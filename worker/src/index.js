/**
 * Report relay for Path of Price Check.
 *
 * The app posts a bug report here and this forwards it to one Discord webhook. The webhook is a
 * Cloudflare secret, so the only thing shipped to users is this Worker's public URL — extracting
 * it from the data bundle yields a rate-limited endpoint, not a credential.
 *
 * Everything the app sends is untrusted. A report reaches a Discord channel and from there,
 * usually, a GitHub issue — both render markdown. The rule enforced below is that a report can
 * only ever *be text*: it cannot mention anyone, link anywhere, or arrive as anything but a PNG.
 */

const LIMITS = {
    body: 8 * 1024 * 1024,
    item: 16 * 1024,
    parse: 64 * 1024,
    comment: 2000,
    meta: 64,
    screenshot: 5 * 1024 * 1024,
};

// Discord's own caps. Enforced here so an oversized report is trimmed rather than 400'd away.
const DISCORD = { title: 256, description: 4096, fieldValue: 1024, total: 6000, threadName: 100 };

const PNG_MAGIC = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];

export default {
    async fetch(request, env) {
        const url = new URL(request.url);
        if (request.method !== 'POST' || url.pathname !== '/report') {
            return new Response(null, { status: 404 });
        }
        if (env.REPORTS_ENABLED === '0') {
            return json(503, 'reporting is turned off');
        }
        if (!webhook_ok(env.DISCORD_WEBHOOK)) {
            console.error('DISCORD_WEBHOOK is unset or not a Discord webhook URL');
            return json(500, 'relay is misconfigured');
        }
        // Requiring JSON forces a CORS preflight on any browser-originated POST, and we answer no
        // OPTIONS — so a random web page cannot use this endpoint even though it is public.
        if (!(request.headers.get('content-type') || '').includes('application/json')) {
            return json(415, 'expected application/json');
        }
        if (Number(request.headers.get('content-length') || 0) > LIMITS.body) {
            return json(413, 'report too large');
        }

        const raw = await request.text().catch(() => null);
        if (raw === null) return json(400, 'unreadable body');
        if (raw.length > LIMITS.body) return json(413, 'report too large');

        let body;
        try {
            body = JSON.parse(raw);
        } catch {
            return json(400, 'malformed JSON');
        }
        if (body === null || typeof body !== 'object' || Array.isArray(body)) {
            return json(400, 'expected a JSON object');
        }

        const report = read_report(body);
        if (report.error) return json(report.status, report.error);

        const ip = request.headers.get('cf-connecting-ip') || '';
        const limited = await rate_limit(env, ip);
        if (limited) return json(429, limited, { 'retry-after': '3600' });

        const id = crypto.randomUUID().split('-')[0];
        const at = new Date().toISOString();

        const res = await post_to_discord(env, report, id, at);
        if (!res.ok) {
            // Discord says why in the body, and the reason is usually a channel/payload mismatch
            // that no amount of staring at this code would reveal. It reaches `wrangler tail`.
            const detail = await res.text().catch(() => '');
            console.error(`discord rejected report ${id}: ${res.status} ${detail.slice(0, 500)}`);
            return json(502, 'could not deliver the report');
        }
        return json(200, null, {}, { ok: true, id });
    },
};

/** Pull exactly the fields we know about. Anything else in the body is ignored, never forwarded. */
function read_report(body) {
    const errs = [];
    const item = field(body.item, LIMITS.item, 'item', errs);
    const parse = field(body.parse, LIMITS.parse, 'parse', errs);
    const comment = field(body.comment, LIMITS.comment, 'comment', errs);

    const meta = {};
    const src = body.meta;
    if (src !== undefined && src !== null) {
        if (typeof src !== 'object' || Array.isArray(src)) {
            errs.push('meta must be an object');
        } else {
            for (const key of META_KEYS) {
                const v = field(src[key], LIMITS.meta, `meta.${key}`, errs);
                if (v) meta[key] = v;
            }
        }
    }

    if (errs.length) return { error: errs[0], status: 400 };
    if (!item) return { error: 'item is required', status: 400 };
    // Every item the game exports has a dashed separator, in every client language. Requiring one
    // costs a legitimate report nothing and turns the endpoint away from anything that is not an
    // item at all.
    if (!/^-{3,}$/m.test(item)) {
        return { error: 'item does not look like PoE clipboard text', status: 400 };
    }

    let png = null;
    if (body.screenshot_png_b64 !== undefined && body.screenshot_png_b64 !== null) {
        if (typeof body.screenshot_png_b64 !== 'string') {
            return { error: 'screenshot_png_b64 must be a string', status: 400 };
        }
        const decoded = decode_png(body.screenshot_png_b64);
        if (decoded.error) return { error: decoded.error, status: decoded.status };
        png = decoded.bytes;
    }

    return { item, parse, comment, meta, png };
}

const META_KEYS = ['version', 'os', 'league', 'bundle'];
const META_LABELS = { version: 'App', os: 'OS', league: 'League', bundle: 'Bundle' };

/**
 * One untrusted string, cleaned. Caps are generous enough that exceeding one is not an accident,
 * so this rejects rather than truncating — a silently half-sent item would be a worse bug report
 * than none.
 */
function field(v, max, name, errs) {
    if (v === undefined || v === null) return '';
    if (typeof v !== 'string') {
        errs.push(`${name} must be a string`);
        return '';
    }
    if (v.length > max) {
        errs.push(`${name} is longer than ${max} characters`);
        return '';
    }
    return clean(v);
}

/**
 * Strip everything that is not text. The bidi overrides are the ones that matter: U+202E and its
 * neighbours reorder a whole line in both Discord and GitHub, so without this a report could be
 * made to *display* as something other than what it says.
 */
function clean(s) {
    return s
        .replace(/\r\n?/g, '\n')
        .replace(/[\u0000-\u0008\u000B\u000C\u000E-\u001F\u007F]/g, '')
        .replace(/[\u200B-\u200F\u202A-\u202E\u2066-\u2069\uFEFF]/g, '')
        .trim();
}

function decode_png(s) {
    const b64 = s.replace(/^data:[^,]*,/, '').replace(/\s+/g, '');
    if ((b64.length / 4) * 3 > LIMITS.screenshot) {
        return { error: 'screenshot too large', status: 413 };
    }

    let bin;
    try {
        bin = atob(b64);
    } catch {
        return { error: 'screenshot is not valid base64', status: 400 };
    }
    const bytes = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);

    // The filename and content type below are ours, not the reporter's, but a channel that accepts
    // arbitrary bytes under a .png name is still a file drop. Check that it really is a PNG.
    if (bytes.length < PNG_MAGIC.length || PNG_MAGIC.some((b, i) => bytes[i] !== b)) {
        return { error: 'screenshot is not a PNG', status: 400 };
    }
    return { bytes };
}

/** Guards against a mistyped secret turning the relay into a request forwarder for some other host. */
function webhook_ok(u) {
    if (typeof u !== 'string' || !u) return false;
    try {
        const p = new URL(u);
        return (
            p.protocol === 'https:' &&
            /^(canary\.|ptb\.)?discord(app)?\.com$/.test(p.hostname) &&
            p.pathname.startsWith('/api/webhooks/')
        );
    } catch {
        return false;
    }
}

/**
 * Per-IP and whole-relay caps. Optional: with no KV binding the relay still works, it just has no
 * brakes. Failures here fail *open* — losing a real report to a KV blip is worse than letting one
 * extra through.
 *
 * Both caps live in **one** key, and it costs one read and one write. Two keys would be the obvious
 * shape and is the wrong one: the free tier's thousand writes a day are the thing the daily cap
 * exists to protect, and a cap that spends two of them per report is guarding a budget it is the
 * largest consumer of. A refusal writes nothing at all.
 *
 * The read-then-write is not atomic and a KV read can be a minute stale, so reports landing together
 * lose increments and a sustained flood is counted against a total that lags it. Both caps are
 * therefore approximate, and approximate in the direction of letting too much through — which is why
 * the daily cap is set well under the write quota rather than up against it.
 */
async function rate_limit(env, ip) {
    if (!env.RL) return null;
    const per_ip = Number(env.MAX_PER_IP_PER_HOUR || 5);
    const per_day = Number(env.MAX_PER_DAY_GLOBAL || 300);
    const now = Date.now();
    try {
        const key = `rl:${new Date(now).toISOString().slice(0, 10)}`;
        const state = JSON.parse((await env.RL.get(key)) || '{}');
        const total = Number(state.total) || 0;
        if (total >= per_day) return 'the relay is over its daily limit, try again tomorrow';

        // The whole value is rewritten anyway, so an address whose hour has run out is dropped here
        // rather than kept until the day rolls over. This is also what bounds the value's size: it
        // holds the last hour's reporters, not the day's.
        const ips = {};
        for (const [k, v] of Object.entries(state.ips || {})) {
            if (now - v.at < 3600_000) ips[k] = v;
        }

        const who = ip ? await ip_key(env, ip) : null;
        const mine = who ? ips[who] : null;
        if (mine && mine.n >= per_ip) return 'too many reports from this address, try again later';
        if (who) ips[who] = { n: (mine ? mine.n : 0) + 1, at: mine ? mine.at : now };

        const next = JSON.stringify({ total: total + 1, ips });
        await env.RL.put(key, next, { expirationTtl: 86400 });
    } catch (e) {
        console.error(`rate limiter unavailable: ${e}`);
    }
    return null;
}

/**
 * What stands in for an address in KV. HMAC and not a digest on purpose: an IPv4 address is 32 bits,
 * so a plain hash of one is a lookup table away from being the address again. The key is a
 * Cloudflare secret and is never itself written to KV, so the namespace holds counts against strings
 * that cannot be turned back into anyone. Set it with `./rotate-rl-salt.sh`; unset, this degrades to
 * exactly that reversible digest rather than to no cap at all.
 */
async function ip_key(env, ip) {
    if (!env.RL_SALT) console.error('RL_SALT is unset — per-IP keys are reversible');
    const bytes = new TextEncoder();
    const key = await crypto.subtle.importKey(
        'raw',
        bytes.encode(env.RL_SALT || 'ppc-reports'),
        { name: 'HMAC', hash: 'SHA-256' },
        false,
        ['sign'],
    );
    const mac = new Uint8Array(await crypto.subtle.sign('HMAC', key, bytes.encode(ip)));
    // 12 of the 32 bytes: a counter key, not a signature. Collisions at this width would mean two
    // reporters sharing an hourly allowance, and there are not 2^48 reporters.
    return [...mac.slice(0, 12)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

async function post_to_discord(env, report, id, at) {
    const url = env.DISCORD_WEBHOOK;
    const form = new FormData();
    form.append('payload_json', JSON.stringify(message(report, id, at, env.DISCORD_FORUM !== '0')));
    form.append(
        'files[0]',
        new Blob([issue_markdown(report, id, at)], { type: 'text/markdown' }),
        'report.md',
    );
    if (report.png) {
        form.append('files[1]', new Blob([report.png], { type: 'image/png' }), 'screenshot.png');
    }
    // No content-type header: fetch sets it with the multipart boundary. ?wait=true so a rejected
    // message comes back as a status rather than a silent 204.
    return fetch(`${url}?wait=true`, { method: 'POST', body: form });
}

function message(report, id, at, forum) {
    const fields = [];
    if (report.comment) {
        fields.push({
            name: 'What the reporter said',
            value: fenced(report.comment, DISCORD.fieldValue),
        });
    }
    for (const key of META_KEYS) {
        if (report.meta[key]) {
            fields.push({ name: META_LABELS[key], value: inline_code(report.meta[key]), inline: true });
        }
    }

    const embed = {
        title: cut(title_of(report.item), DISCORD.title),
        description: fenced(report.item, DISCORD.description),
        color: 0xc0a060,
        fields,
        footer: { text: `report ${id} · report.md is ready to paste into an issue` },
        timestamp: at,
    };
    if (report.png) embed.image = { url: 'attachment://screenshot.png' };

    const payload = {
        username: 'Path of Price Check',
        // The one setting that makes @everyone in a report inert. Everything else is cosmetic;
        // without this, any reporter can ping the whole server.
        allowed_mentions: { parse: [] },
        embeds: [within_budget(embed, report.item)],
    };
    // A forum channel takes a post, not a message: `thread_name` is what makes one, and a webhook
    // aimed at a forum is rejected without it. Tags stay manual — every report carrying the same
    // one would sort nothing.
    if (forum) payload.thread_name = thread_name(report.item, id);
    return payload;
}

/**
 * The forum post's title. The report id rides along on the end and is reserved room before the
 * name is trimmed, because two reports about the same base are otherwise the same post title.
 */
function thread_name(item, id) {
    const suffix = ` · ${id}`;
    const name = title_of(item).replace(/\s+/g, ' ').trim();
    return cut(name, DISCORD.threadName - suffix.length) + suffix;
}

/**
 * The attachment, written so it can be pasted into a GitHub issue unedited. Deliberately not a
 * link to anything: the report travels as its own text.
 */
function issue_markdown(report, id, at) {
    const out = [`### Item\n\n${fence(report.item)}`];
    if (report.parse) out.push(`### Parse output\n\n${fence(report.parse)}`);
    if (report.comment) out.push(`### Reported problem\n\n${fence(report.comment)}`);

    const env = [`- Report: \`${id}\` (${at})`];
    for (const key of META_KEYS) {
        if (report.meta[key]) env.push(`- ${META_LABELS[key]}: \`${report.meta[key]}\``);
    }
    out.push(`### Environment\n\n${env.join('\n')}`);

    return out.join('\n\n') + '\n';
}

/**
 * A name for the embed. The first block of an item export is its labelled header plus the name and
 * base, so the unlabelled lines are the ones worth showing. Getting this wrong costs a clumsy
 * title and nothing else — the full text is directly below it.
 */
function title_of(item) {
    const head = item.split(/\n-{3,}\n/)[0] || item;
    const names = head
        .split('\n')
        .map((s) => s.trim())
        .filter((s) => s && !s.includes(': '));
    return names.slice(0, 2).join(' — ') || 'Item report';
}

/**
 * Wrap untrusted text so Discord and GitHub render it as characters. Inside a fence `@everyone` is
 * a word and `[click here](http://…)` is punctuation; the only way out is a fence of its own,
 * which is what the substitution prevents.
 */
function fence(s) {
    return '```text\n' + s.replace(/```/g, "'''") + '\n```';
}

/** `max` is the budget for the whole fenced block, not the text inside it. */
function fenced(s, max) {
    return fence(cut(s, Math.max(16, max - 20)));
}

function inline_code(s) {
    return '`' + s.replace(/`/g, "'") + '`';
}

function cut(s, max) {
    return s.length <= max ? s : s.slice(0, Math.max(1, max - 1)) + '…';
}

/** Discord counts every embed string against one 6000-character budget and 400s the lot if over. */
function within_budget(embed, item) {
    const size = () => JSON.stringify(embed).length;
    while (size() > DISCORD.total && embed.fields.length) embed.fields.pop();
    if (size() > DISCORD.total) {
        // Re-fence from the original text rather than trimming the fenced string, which would eat
        // its own closing delimiter.
        embed.description = fenced(item, Math.max(80, embed.description.length - (size() - DISCORD.total)));
    }
    return embed;
}

function json(status, error, headers = {}, payload = null) {
    const body = payload || { ok: false, error };
    return new Response(JSON.stringify(body), {
        status,
        headers: { 'content-type': 'application/json; charset=utf-8', ...headers },
    });
}
