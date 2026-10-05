const TitanDB = require('./titandb_driver');

const db = new TitanDB();

// A simple script to prove the RAG Context Batching works.
// We set multiple "document chunks" in the DB, and then the AI 
// fetches them all simultaneously in a single network round trip.
async function testRAG() {
    console.log("Seeding Database with Weather Documents...\n");
    await db.set("weather:london", '{"temp": 15, "humidity": 80}');
    await db.set("weather:newyork", '{"temp": 22, "humidity": 50}');
    await db.set("weather:tokyo", '{"temp": 28, "humidity": 70}');

    console.log("AI Agent requests all 3 cities at once using OP_GET_CONTEXT...");
    const keysToFetch = ["weather:london", "weather:newyork", "weather:tokyo"];
    
    const startTime = Date.now();
    
    // FETCH 3 DOCUMENTS IN ONE TCP PACKET
    const results = await db.getContext(keysToFetch);
    
    const latency = Date.now() - startTime;

    console.log(`\n[RAG BATCH RESULT in ${latency}ms]`);
    results.forEach((res, index) => {
        console.log(`- ${keysToFetch[index]} -> ${res}`);
    });

    process.exit(0);
}

setTimeout(testRAG, 500); // Wait for connection to establish
