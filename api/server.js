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

    app.get('/api/weather/:city', async (req, res) => {
        const city = req.params.city.toLowerCase();
        const cacheKey = `weather:${city}`;
        
        // console.log("got req for", city); // checking if route hits

        const coordinates = {
            'london': 'latitude=51.5085&longitude=-0.1257',
            'newyork': 'latitude=40.7143&longitude=-74.006',
            'tokyo': 'latitude=35.6895&longitude=139.6917'
        };

        if (!coordinates[city]) {
            return res.status(404).send({ error: "city not found" });
        }

        const startTime = Date.now();

        const cachedData = await db.get(cacheKey);
        
        if (cachedData) {
            const latency = Date.now() - startTime;
            // console.log("cache hit in", latency, "ms"); 
            
            res.setHeader('X-Cache', 'HIT');
            res.setHeader('X-Latency-Ms', latency);
            res.setHeader('Content-Type', 'application/json');
            return res.send(cachedData);
        }

        console.log(`fetching from open-meteo for ${city}... cache miss`);
        try {
            const url = `https://api.open-meteo.com/v1/forecast?${coordinates[city]}&hourly=temperature_2m,relative_humidity_2m,wind_speed_10m&past_days=7`;
            const response = await fetch(url);
            const jsonText = await response.text(); 
            
            // save it back to our db
            await db.set(cacheKey, jsonText);

            const latency = Date.now() - startTime;
            // console.log("saved to db");
            
            res.setHeader('X-Cache', 'MISS');
            res.setHeader('X-Latency-Ms', latency);
            res.setHeader('Content-Type', 'application/json');
            res.send(jsonText);
        } catch (err) {
            console.log("error fetching api:", err);
            res.status(500).send({ error: "api failed" });
        }
    });

    app.listen(3000, () => {
        // console.log(`worker ${process.pid} listening`);
    });
}
