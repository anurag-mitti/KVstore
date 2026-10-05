const net = require('net');

class TitanDB {
    constructor(host = '127.0.0.1', port = 8080) {
        this.client = new net.Socket();
        this.client.connect(port, host, () => {
            // console.log('connected to cpp backend');
        });
        
        this.queue = [];
        
        this.client.on('data', (data) => {
            if (this.queue.length === 0) return;
            const { resolve, reject, isBatch } = this.queue.shift();
            
            // reading from network byte order
            const totalLen = data.readUInt32BE(0); 
            const status = data.readUInt8(4);
            
            // console.log("got response len:", totalLen, "status:", status);

            if (status === 0) {
                if (isBatch) {
                    const numVals = data.readUInt16BE(5);
                    const results = [];
                    let offset = 7;
                    for (let i = 0; i < numVals; i++) {
                        const vlen = data.readUInt32BE(offset);
                        offset += 4;
                        results.push(data.toString('utf8', offset, offset + vlen));
                        offset += vlen;
                    }
                    resolve(results);
                } else if (totalLen > 1) {
                    const valLen = data.readUInt32BE(5);
                    const val = data.toString('utf8', 9, 9 + valLen);
                    resolve(val);
                } else {
                    resolve(true);
                }
            } else {
                resolve(null);
            }
        });
        
        this.client.on('error', (err) => {
            console.log("tcp error:", err);
        });
    }

    set(key, value) {
        return new Promise((resolve, reject) => {
            const keyBuf = Buffer.from(key, 'utf8');
            const valBuf = Buffer.from(value, 'utf8');
            
            const totalLen = 1 + 2 + keyBuf.length + 4 + valBuf.length;
            const packet = Buffer.alloc(4 + totalLen);
            
            packet.writeUInt32BE(totalLen, 0);
            packet.writeUInt8(1, 4); 
            packet.writeUInt16BE(keyBuf.length, 5);
            keyBuf.copy(packet, 7);
            packet.writeUInt32BE(valBuf.length, 7 + keyBuf.length);
            valBuf.copy(packet, 11 + keyBuf.length);
            
            this.queue.push({ resolve, reject, isBatch: false });
            this.client.write(packet);
        });
    }

    get(key) {
        return new Promise((resolve, reject) => {
            const keyBuf = Buffer.from(key, 'utf8');
            
            const totalLen = 1 + 2 + keyBuf.length;
            const packet = Buffer.alloc(4 + totalLen);
            
            packet.writeUInt32BE(totalLen, 0);
            packet.writeUInt8(0, 4); 
            packet.writeUInt16BE(keyBuf.length, 5);
            keyBuf.copy(packet, 7);
            
            this.queue.push({ resolve, reject, isBatch: false });
            this.client.write(packet);
        });
    }

    getContext(keys) {
        return new Promise((resolve, reject) => {
            const keyBufs = keys.map(k => Buffer.from(k, 'utf8'));
            
            let payloadSize = 2; 
            for (const b of keyBufs) payloadSize += 2 + b.length;
            
            const totalLen = 1 + payloadSize;
            const packet = Buffer.alloc(4 + totalLen);
            
            packet.writeUInt32BE(totalLen, 0);
            packet.writeUInt8(2, 4); 
            packet.writeUInt16BE(keys.length, 5);
            
            let offset = 7;
            for (const b of keyBufs) {
                packet.writeUInt16BE(b.length, offset);
                offset += 2;
                b.copy(packet, offset);
                offset += b.length;
            }
            
            this.queue.push({ resolve, reject, isBatch: true });
            this.client.write(packet);
        });
    }
}

module.exports = TitanDB;
