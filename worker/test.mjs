// Contract tests for the report relay. `node --test worker/test.mjs` — no dependencies, no
// network: Discord is a stub, so what is asserted is the exact request the Worker would send.
//
// The point of most of these is that a reporter controls every string in the payload. Each one
// pins a way that control could turn into something other than text.
import { test } from 'node:test';
import assert from 'node:assert/strict';

import worker from './src/index.js';

const WEBHOOK = 'https://discord.com/api/webhooks/123/abc';
const ITEM = 'Item Class: Bows\nRarity: Rare\nDoom Song\nSpine Bow\n--------\nQuality: +20%';

/** Runs one report and returns what Discord would have received. */
async function send(body, env = {}) {
    let seen = null;
    const real = globalThis.fetch;
    globalThis.fetch = async (url, init) => {
        seen = { url, form: init.body };
        return new Response('{}', { status: 200 });
    };
    try {
        const res = await worker.fetch(
            new Request('https://relay.example/report', {
                method: 'POST',
                headers: {
                    'content-type': 'application/json',
                    'cf-connecting-ip': env.ip || '203.0.113.7',
                },
                body: JSON.stringify(body),
            }),
            { DISCORD_WEBHOOK: WEBHOOK, ...env },
        );
        const payload = seen && JSON.parse(seen.form.get('payload_json'));
        const files = {};
        if (seen) {
            for (const [k, v] of seen.form.entries()) {
                if (k.startsWith('files[')) files[v.name] = v;
            }
        }
        return { res, json: await res.json(), payload, files, url: seen?.url };
    } finally {
        globalThis.fetch = real;
    }
}

test('a plain report reaches Discord and comes back with an id', async () => {
    const { res, json, payload, files } = await send({ item: ITEM });
    assert.equal(res.status, 200);
    assert.equal(json.ok, true);
    assert.match(json.id, /^[0-9a-f]{8}$/);
    assert.equal(payload.embeds[0].title, 'Doom Song — Spine Bow');
    assert.ok(files['report.md'], 'report.md is always attached');
});

test('a report is posted as a forum thread, named and disambiguated', async () => {
    const { json, payload } = await send({ item: ITEM });
    assert.equal(payload.thread_name, `Doom Song — Spine Bow · ${json.id}`);
});

test('a long name is trimmed but the id always survives', async () => {
    const long = `Item Class: Bows\nRarity: Rare\n${'Doom '.repeat(40)}\nSpine Bow\n--------\nQuality: +20%`;
    const { json, payload } = await send({ item: long });
    assert.ok(payload.thread_name.length <= 100);
    assert.ok(payload.thread_name.endsWith(` · ${json.id}`));
});

test('a thread name is a single line', async () => {
    const { payload } = await send({ item: ITEM });
    assert.ok(!payload.thread_name.includes('\n'));
});

test('DISCORD_FORUM=0 posts a plain message instead', async () => {
    const { payload } = await send({ item: ITEM }, { DISCORD_FORUM: '0' });
    assert.equal(payload.thread_name, undefined);
    assert.equal(payload.embeds.length, 1);
});

test('mentions cannot ping anyone', async () => {
    const { payload } = await send({ item: ITEM, comment: '@everyone @here <@&999>' });
    assert.deepEqual(payload.allowed_mentions, { parse: [] });
});

test('a comment cannot break out of its code fence', async () => {
    const { payload } = await send({
        item: ITEM,
        comment: '```\n[click me](https://evil.invalid)',
    });
    const field = payload.embeds[0].fields.find((f) => f.name === 'What the reporter said');
    assert.ok(!field.value.includes('```\n['), 'the injected fence is neutralised');
    assert.ok(field.value.startsWith('```text\n') && field.value.endsWith('\n```'));
});

test('bidi overrides and control characters are stripped', async () => {
    const { payload, files } = await send({
        item: ITEM,
        comment: 'safe\u202egnorw sdaer\u202c\u200b\u0000text',
    });
    const body = JSON.stringify(payload) + (await files['report.md'].text());
    for (const ch of ['\u202e', '\u202c', '\u200b', '\u0000']) {
        assert.ok(!body.includes(ch), `${JSON.stringify(ch)} survived`);
    }
});

test('unknown fields are dropped rather than forwarded', async () => {
    const { payload } = await send({
        item: ITEM,
        username: 'GGG Support',
        avatar_url: 'https://evil.invalid/a.png',
        content: '@everyone',
        embeds: [{ title: 'injected' }],
    });
    assert.equal(payload.username, 'Path of Price Check');
    assert.equal(payload.content, undefined);
    assert.equal(payload.embeds.length, 1);
    assert.equal(payload.embeds[0].title, 'Doom Song — Spine Bow');
});

test('the embed stays inside Discord\'s 6000-character budget', async () => {
    const long = 'Item Class: Bows\nRarity: Rare\nDoom Song\nSpine Bow\n--------\n' + 'x'.repeat(15000);
    const { res, payload } = await send({ item: long, comment: 'y'.repeat(1900) });
    assert.equal(res.status, 200);
    assert.ok(JSON.stringify(payload.embeds[0]).length <= 6000);
});

test('oversized fields are refused, not silently trimmed', async () => {
    const { res, json } = await send({ item: ITEM, comment: 'x'.repeat(2001) });
    assert.equal(res.status, 400);
    assert.match(json.error, /comment is longer/);
});

test('non-string fields are refused', async () => {
    for (const bad of [{ item: 5 }, { item: ITEM, parse: {} }, { item: ITEM, meta: [] }]) {
        const { res } = await send(bad);
        assert.equal(res.status, 400);
    }
});

test('text that is not an item is refused', async () => {
    const { res, json } = await send({ item: 'buy cheap currency at example.invalid' });
    assert.equal(res.status, 400);
    assert.match(json.error, /does not look like/);
});

test('a screenshot must really be a PNG', async () => {
    const gif = Buffer.from('GIF89a' + 'x'.repeat(64)).toString('base64');
    const { res, json } = await send({ item: ITEM, screenshot_png_b64: gif });
    assert.equal(res.status, 400);
    assert.match(json.error, /not a PNG/);
});

test('a real PNG is attached under a filename we chose', async () => {
    const png = Buffer.concat([
        Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
        Buffer.alloc(32),
    ]).toString('base64');
    const { payload, files } = await send({ item: ITEM, screenshot_png_b64: png });
    assert.ok(files['screenshot.png']);
    assert.equal(payload.embeds[0].image.url, 'attachment://screenshot.png');
});

test('only POST /report exists', async () => {
    for (const [method, path] of [['GET', '/report'], ['POST', '/'], ['POST', '/anything']]) {
        const res = await worker.fetch(
            new Request(`https://relay.example${path}`, { method }),
            { DISCORD_WEBHOOK: WEBHOOK },
        );
        assert.equal(res.status, 404);
    }
});

test('a non-JSON content type is refused, so browsers must preflight', async () => {
    const res = await worker.fetch(
        new Request('https://relay.example/report', {
            method: 'POST',
            headers: { 'content-type': 'text/plain' },
            body: JSON.stringify({ item: ITEM }),
        }),
        { DISCORD_WEBHOOK: WEBHOOK },
    );
    assert.equal(res.status, 415);
});

test('a misconfigured webhook is refused before anything is sent', async () => {
    for (const bad of [undefined, 'https://evil.invalid/api/webhooks/1/2', 'not a url']) {
        const { res } = await send({ item: ITEM }, { DISCORD_WEBHOOK: bad });
        assert.equal(res.status, 500);
    }
});

test('REPORTS_ENABLED=0 turns the relay off', async () => {
    const { res } = await send({ item: ITEM }, { REPORTS_ENABLED: '0' });
    assert.equal(res.status, 503);
});

/** A KV stand-in that counts its writes, because how many there are is part of the contract. */
function kv() {
    const store = new Map();
    let writes = 0;
    return {
        store,
        writes: () => writes,
        only: () => JSON.parse([...store.values()][0]),
        get: async (k) => store.get(k) ?? null,
        put: async (k, v) => {
            writes++;
            store.set(k, v);
        },
    };
}

test('the per-IP cap rejects once it is reached', async () => {
    const RL = kv();
    const env = { RL, MAX_PER_IP_PER_HOUR: '2' };
    for (let i = 0; i < 2; i++) {
        assert.equal((await send({ item: ITEM }, env)).res.status, 200);
    }
    const { res, json } = await send({ item: ITEM }, env);
    assert.equal(res.status, 429);
    assert.match(json.error, /too many reports/);
});

test('the whole-relay cap rejects once it is reached', async () => {
    const RL = kv();
    const env = { RL, MAX_PER_DAY_GLOBAL: '1' };
    assert.equal((await send({ item: ITEM }, env)).res.status, 200);
    const { res, json } = await send({ item: ITEM }, { ...env, ip: '198.51.100.9' });
    assert.equal(res.status, 429);
    assert.match(json.error, /daily limit/);
});

test('a report costs one KV write, and a refusal costs none', async () => {
    const RL = kv();
    const env = { RL, MAX_PER_IP_PER_HOUR: '2' };
    await send({ item: ITEM }, env);
    await send({ item: ITEM }, env);
    assert.equal(RL.writes(), 2);
    assert.equal(RL.store.size, 1); // both caps in one key, not one key each
    assert.equal((await send({ item: ITEM }, env)).res.status, 429);
    assert.equal(RL.writes(), 2);
});

test('the address is stored as an HMAC of it, never as itself', async () => {
    const RL = kv();
    const salt = 'a'.repeat(64);
    const env = { RL, RL_SALT: salt, ip: '203.0.113.7' };
    await send({ item: ITEM }, env);

    const [key, value] = [...RL.store.entries()][0];
    assert.ok(!key.includes('203.0.113.7') && !value.includes('203.0.113.7'), `${key} ${value}`);
    const who = Object.keys(RL.only().ips);
    assert.equal(who.length, 1);
    assert.match(who[0], /^[0-9a-f]{24}$/);

    // The same address has to land on the same entry or the cap counts to one forever — and a
    // different salt on a different one, which is what makes the entry depend on the secret.
    await send({ item: ITEM }, env);
    assert.deepEqual(Object.keys(RL.only().ips), who);
    assert.equal(RL.only().ips[who[0]].n, 2);
    await send({ item: ITEM }, { ...env, RL_SALT: 'b'.repeat(64) });
    assert.equal(Object.keys(RL.only().ips).length, 2);
});

test('an address that has gone quiet for its hour is dropped from the value', async () => {
    const RL = kv();
    const [key] = [`rl:${new Date().toISOString().slice(0, 10)}`];
    const stale = 'f'.repeat(24);
    RL.store.set(key, JSON.stringify({ total: 4, ips: { [stale]: { n: 5, at: Date.now() - 7200_000 } } }));
    await send({ item: ITEM }, { RL });

    const state = RL.only();
    assert.equal(state.total, 5, 'the day total is not what expires');
    assert.ok(!(stale in state.ips));
    assert.equal(Object.keys(state.ips).length, 1);
});

test('a broken rate limiter fails open', async () => {
    const RL = {
        get: async () => { throw new Error('KV is down'); },
        put: async () => {},
    };
    const { res } = await send({ item: ITEM }, { RL });
    assert.equal(res.status, 200);
});

test('report.md carries the whole report, ready to paste', async () => {
    const { files } = await send({
        item: ITEM,
        parse: "modifier 3 matched no stat record",
        comment: 'wrong affix',
        meta: { version: '0.6.17', os: 'linux' },
    });
    const md = await files['report.md'].text();
    assert.ok(md.includes('### Item') && md.includes('Doom Song'));
    assert.ok(md.includes('### Parse output') && md.includes('matched no stat record'));
    assert.ok(md.includes('### Reported problem') && md.includes('wrong affix'));
    assert.ok(md.includes('- App: `0.6.17`') && md.includes('- OS: `linux`'));
});
