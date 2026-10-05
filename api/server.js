const express = require('express');
const cluster = require('cluster');
const os = require('os');
const TitanDB = require('./titandb_driver');

const numCPUs = os.cpus().length;

if (cluster.isPrimary || cluster.isMaster) {
    console.log(`master running on pid ${process.pid}`);
    
    // fork for every core
    for (let i = 0; i < numCPUs; i++) {
        cluster.fork();
    }

    cluster.on('exit', (worker, code, signal) => {
        console.log(`worker ${worker.process.pid} died. restarting...`);
        cluster.fork();
    });

} else {
    // this runs on all cpu cores
    const app = express();
    const db = new TitanDB();
    
    // Store active fetches to dedupe cache stampedes per-worker
    const activeRequests = new Map();

    // Cache Config
    const SOFT_TTL_MS = 55 * 60 * 1000; // 55 minutes
    const HARD_TTL_MS = 120 * 60 * 1000; // 2 hours

    // Helper to fetch from external API and cache asynchronously
    async function fetchAndCache(city, url, cacheKey) {
        try {
            const response = await fetch(url);
            if (!response.ok) throw new Error(`API returned ${response.status}`);
            const jsonText = await response.text();
            
            // Wrap the data with a timestamp for Stale-While-Revalidate
            const payload = JSON.stringify({
                savedAt: Date.now(),
                data: jsonText
            });

            // Fire and forget the cache write (Non-blocking)
            db.set(cacheKey, payload).catch(err => {
                console.error(`[Cache Error] Failed to write ${cacheKey}:`, err);
            });
            
            return jsonText;
        } catch (err) {
            console.error(`[API Error] Failed to fetch weather for ${city}:`, err);
            throw err;
        } finally {
            // Always cleanup the dedup map so future requests can fetch again
            activeRequests.delete(cacheKey);
        }
    }

    app.get('/api/weather/:city', async (req, res) => {
        const city = req.params.city.toLowerCase();
        const cacheKey = `weather:${city}`;
        
        const coordinates = {
            'london': 'latitude=51.5085&longitude=-0.1257',
            'newyork': 'latitude=40.7143&longitude=-74.006',
            'tokyo': 'latitude=35.6895&longitude=139.6917'
        };

        if (!coordinates[city]) {
            return res.status(404).send({ error: "city not found" });
        }
        
        const url = `https://api.open-meteo.com/v1/forecast?${coordinates[city]}&hourly=temperature_2m,relative_humidity_2m,wind_speed_10m&past_days=7`;
        const startTime = Date.now();

        // 1. Fail-Open Cache Get
        let cachedPayload = null;
        try {
            const rawData = await db.get(cacheKey);
            if (rawData) {
                cachedPayload = JSON.parse(rawData);
            }
        } catch (err) {
            console.error(`[Cache Error] Failed to read ${cacheKey}:`, err);
        }

        if (cachedPayload) {
            const ageMs = Date.now() - cachedPayload.savedAt;
            const latency = Date.now() - startTime;
            res.setHeader('X-Latency-Ms', latency);
            res.setHeader('Content-Type', 'application/json');

            // 2. FRESH HIT
            if (ageMs < SOFT_TTL_MS) {
                res.setHeader('X-Cache', 'HIT');
                return res.send(cachedPayload.data);
            }
            
            // 3. STALE-WHILE-REVALIDATE HIT
            if (ageMs < HARD_TTL_MS) {
                res.setHeader('X-Cache', 'STALE');
                
                // Trigger background fetch if one isn't already running
                if (!activeRequests.has(cacheKey)) {
                    console.log(`[SWR] Triggering background fetch for ${city}`);
                    const fetchPromise = fetchAndCache(city, url, cacheKey);
                    activeRequests.set(cacheKey, fetchPromise);
                }
                
                // Return stale data immediately!
                return res.send(cachedPayload.data);
            }
            // If we reach here, Hard TTL expired. We must block for fresh data.
        }

        // 4. CACHE MISS (or Hard Expired)
        
        // Promise Deduping (Request Coalescing)
        if (activeRequests.has(cacheKey)) {
            console.log(`[DEDUPE] Piggybacking on active fetch for ${city}`);
            try {
                const jsonText = await activeRequests.get(cacheKey);
                const latency = Date.now() - startTime;
                res.setHeader('X-Cache', 'DEDUPED');
                res.setHeader('X-Latency-Ms', latency);
                res.setHeader('Content-Type', 'application/json');
                return res.send(jsonText);
            } catch (err) {
                return res.status(502).send({ error: "Weather service unavailable" });
            }
        }

        // Fetch new data and register the promise
        console.log(`[MISS] Fetching fresh data for ${city}`);
        const fetchPromise = fetchAndCache(city, url, cacheKey);
        activeRequests.set(cacheKey, fetchPromise);

        try {
            const jsonText = await fetchPromise;
            const latency = Date.now() - startTime;
            res.setHeader('X-Cache', 'MISS');
            res.setHeader('X-Latency-Ms', latency);
            res.setHeader('Content-Type', 'application/json');
            return res.send(jsonText);
        } catch (err) {
            return res.status(502).send({ error: "Weather service unavailable" });
        }
    });

    app.listen(3000, () => {
        // console.log(`worker ${process.pid} listening`);
    });
}
