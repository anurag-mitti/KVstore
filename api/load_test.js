const http = require('http');

const CONCURRENCY = 50;
const DURATION_SEC = 10;
const URL = 'http://localhost:3000/api/crypto/BTCUSDT';

let requestCount = 0;
let startTime = Date.now();
let running = true;

console.log(`Starting load test... ${CONCURRENCY} concurrent users for ${DURATION_SEC} seconds.`);
console.log(`Hammering TitanDB through Express... Please wait.`);

// Stop the test after 10 seconds
setTimeout(() => {
    running = false;
    const elapsed = (Date.now() - startTime) / 1000;
    console.log(`\n============================`);
    console.log(`        RESULTS             `);
    console.log(`============================`);
    console.log(`Total Requests: ${requestCount}`);
    console.log(`Requests/sec:   ${(requestCount / elapsed).toFixed(2)} ops/sec`);
    console.log(`============================`);
    process.exit(0);
}, DURATION_SEC * 1000);

// Keep making requests constantly
function makeRequest() {
    if (!running) return;
    
    http.get(URL, (res) => {
        // We must consume the data stream for the 'end' event to fire
        res.on('data', () => {}); 
        res.on('end', () => {
            requestCount++;
            makeRequest(); // Instantly make the next request
        });
    }).on('error', (err) => {
        console.error("Connection error:", err.message);
    });
}

// Spawn 50 concurrent "users"
for (let i = 0; i < CONCURRENCY; i++) {
    makeRequest();
}
