# Report relay

A Cloudflare Worker that takes a bug report from the app and posts it to one Discord channel. It
exists so the app ships **no credential**: the binary carries this Worker's public URL — an
endpoint like every other host it talks to, not a secret — and the Discord webhook lives only in
Cloudflare. Reading the URL out of the binary gets you a rate-limited endpoint that can post a
formatted item report to a private channel, and nothing else.

Nothing above this is automated. A report arrives in Discord with a `report.md` attached that is
written to be pasted into a GitHub issue unedited, by hand, if it turns out to be worth one.

```
app  --HTTPS-->  ppc-reports.<subdomain>.workers.dev  --webhook-->  #ppc-reports
                 (public URL, compiled in)             (Cloudflare secret)
```

The app side — the button, the dialog that previews the whole payload, the screenshot and the
request — is [docs/reporting.md](../docs/reporting.md). `$PPC_REPORT_URL` overrides the endpoint,
which is how the app is tested against something other than the live channel.

## Setup, once

### 1. Discord — the channel and the webhook

1. In your server, **create a forum channel** for this, e.g. `#ppc-reports`. It has to be a forum,
   not a text channel: each report is posted as its own thread, so triage is Discord's own
   open/resolved state and its tags rather than a convention you have to remember. Discord cannot
   convert a text channel into a forum, so this is decided when the channel is made.
2. **Make it private.** Edit Channel → Permissions → deny `@everyone` *View Channel*. Every report
   is text a stranger typed; it does not belong in a channel anyone can read.
3. Optionally add **tags** — `parse`, `data-repo`, `needs-capture`, `wontfix`. Reports arrive
   untagged; nothing applies one automatically, because a tag on every post sorts nothing.
4. Edit Channel → **Integrations** → **Webhooks** → **New Webhook**. Name it (the name is
   overridden per-message anyway), confirm the channel, then **Copy Webhook URL**.
5. Do not paste that URL anywhere else — not a commit, not an issue, not a screenshot. GitHub scans
   for them and Discord revokes the ones it hears about.

The URL looks like `https://discord.com/api/webhooks/<id>/<token>`. You need *Manage Webhooks* on
the server, which as owner you have.

### 2. Cloudflare — account id and an API token

You already have the account (the GitHub SSO login is fine; API tokens work the same).

- **Account ID** — <https://dash.cloudflare.com> → **Workers & Pages**. The ID is in the right-hand
  sidebar, and it is also the hex string in the dashboard URL.
- **API token** — <https://dash.cloudflare.com/profile/api-tokens> → **Create Token** → **Custom
  token**, with:
  - `Account` → `Workers Scripts` → **Edit**
  - `Account` → `Workers KV Storage` → **Edit** *(step 6 creates a namespace with it)*
  - Account Resources → Include → your account
  
  The **Edit Cloudflare Workers** template also works, but it asks for zone permissions this Worker
  does not need. Copy the token when it is shown — it is not shown again.
- **workers.dev subdomain** — if you have never deployed a Worker, the first deploy asks you to
  claim one. Any name; it becomes `ppc-reports.<subdomain>.workers.dev`.

### 3. Local credentials

```sh
cd worker
cp .env.example .env
$EDITOR .env          # CLOUDFLARE_API_TOKEN and CLOUDFLARE_ACCOUNT_ID
```

`.env` is gitignored. It lives on your machine and nowhere else — **no repo secret, no workflow, no
CI**. That is the entire access control for deploying this thing, and it is why neither this repo
nor the data repo may ever gain an Actions secret for it.

### 4. Deploy

```sh
./publish.sh
```

Prints the URL. Needs `node` and `npx`; wrangler is fetched on demand, so there is no `node_modules`
in the repo.

### 5. Set the webhook

```sh
./rotate-webhook.sh
```

Paste the Discord URL at the prompt. It goes straight to Cloudflare's secret store — not into
`.env`, not into any file. The same script replaces it later.

### 6. Rate limiting

Do not skip this. The caps live in `[vars]` but they are read only when the KV binding exists — with
no `RL` binding the limiter returns before it looks at them, so the relay is a public endpoint that
posts to your Discord as fast as anyone cares to ask.

```sh
npx --yes wrangler kv namespace create ppc-reports-rl
```

Put the printed id into the `[[kv_namespaces]]` block in `wrangler.toml`, uncomment it, and
`./publish.sh` again. Then:

```sh
./rotate-rl-salt.sh
```

Defaults are 5 reports per IP per hour and 600 per day across the whole relay — both in `[vars]`.

Both caps live in **one** KV entry, `rl:<date>`, so an accepted report costs one read and one write
and a refusal costs a read and nothing else. That makes `MAX_PER_DAY_GLOBAL` the day's write budget
directly: the free tier allows 1000 writes, and at one per report the cap is the ceiling on how many
are spent. The two-key version cost two writes per report and one per daily refusal, so the same
protection cost more than double and the ceiling was not a number you could read off a var.

Do not spend the remaining 400 on a higher cap. Two things live there. A limiter that cannot write
fails open, so exhausting the quota does not throttle the relay, it un-throttles it for the rest of
the day. And the count is approximate in exactly the case it exists for: KV reads can be up to a
minute stale, so a sustained flood is read against a counter that lags it and writes more than the
cap says. The margin is what absorbs that. The quota is also **per account**, not per namespace — a
second KV-using Worker on the same account spends the same 1000.

Both the counter's day (`rl:<date>`, UTC) and the quota's reset are UTC, so they roll over together.
If you want a cap that is exact rather than approximate, that is a Durable Object, not a bigger
number here.

Inside that entry the day's total is a number and each reporter is `HMAC-SHA-256(RL_SALT, address)`
truncated to 12 bytes, holding a count and the start of its hour. Entries older than an hour are
dropped on the next write, so the value is the last hour's reporters rather than the day's. The salt
is a Cloudflare secret, is never written to KV, and nobody needs to know it — `rotate-rl-salt.sh`
generates it and does not print it. **Without it the limiter still works**, but an IPv4 address is 32
bits, so the stored digest would be a lookup table away from the address; `wrangler tail` says so on
every report until it is set. This paragraph is what [PRIVACY.md](../PRIVACY.md) promises about the
relay's storage — a change here is a change there.

### 7. Check it end to end

```sh
./send-test.sh https://ppc-reports.<subdomain>.workers.dev
./send-test.sh https://ppc-reports.<subdomain>.workers.dev --hostile
```

The first posts a real fixture item. The second posts what a malicious reporter would send —
`@everyone`, a masked link, a code-fence break, a right-to-left override. **Nobody should be
pinged, no link should be clickable, and every character should appear literally.** If any of that
is untrue, stop and say so rather than shipping a release that points at this.

## Day to day

| | |
| --- | --- |
| Mark a report handled | close the forum post. Deleting it works too, but a wording that comes back in three months is then gone. |
| Change the Discord channel | make a new webhook, `./rotate-webhook.sh`. Clients never notice. |
| Replace the rate limiter's salt | `./rotate-rl-salt.sh`. Every open hourly counter is forgotten, which is all it costs. |
| Move back to a text channel | `DISCORD_FORUM = "0"` in `wrangler.toml`, `./publish.sh`. It has to match the channel: a forum rejects a message with no thread name and a text channel rejects one that has it. |
| Turn reporting off | `REPORTS_ENABLED = "0"` in `wrangler.toml`, `./publish.sh`. |
| Turn it off *now* | `npx --yes wrangler delete` — the app treats a dead endpoint as a dropped report. |
| Watch it | `npx --yes wrangler tail` |
| Run the tests | `node --test worker/test.mjs` |

Request logging (Workers Logs) is off in `wrangler.toml`, deliberately: retaining request metadata
would undercut the one thing this feature promises. `wrangler tail` still shows errors live.

## The payload

`POST /report`, `content-type: application/json`. Every field is a string; only `item` is required.

```json
{
  "item": "Item Class: Bows\nRarity: Rare\n…",
  "parse": "[item]   parsed: rarity=3 class='Bows' …",
  "comment": "the third modifier reads as a suffix",
  "screenshot_png_b64": "iVBORw0KGgo…",
  "meta": { "version": "0.6.17", "os": "linux", "league": "Standard", "bundle": "2026-08-01" }
}
```

| Field | Cap | Notes |
| --- | --- | --- |
| `item` | 16 KiB | required; must contain a `-----` separator line, or it is refused as not-an-item |
| `parse` | 64 KiB | free-form; whatever the app's own diagnostic dump prints |
| `comment` | 2000 | what the user typed |
| `screenshot_png_b64` | 5 MiB decoded | must decode to a real PNG; a `data:` prefix is tolerated |
| `meta.*` | 64 each | all optional |

Replies `200 {"ok":true,"id":"a1b2c3d4"}`; `400` malformed, `413` too large, `415` wrong
content-type, `429` rate-limited, `503` switched off, `502` Discord refused it. The app should treat
every non-200 the same way it treats any other failure — drop it, log it, say nothing.

Over-cap fields are **refused, not truncated**: half an item is a worse bug report than none.

## What is enforced, and why

Everything in the payload is written by a stranger and ends up somewhere that renders markdown —
Discord, then usually a GitHub issue. Each of these is a way that could become something other than
text, and each has a test in `test.mjs`:

- **`allowed_mentions: {parse: []}`** — the one thing that makes `@everyone` in a report inert. All
  the escaping in the world does not help without it.
- **Everything user-typed is inside a code fence**, and a `` ``` `` in the input is rewritten so it
  cannot close one. Inside a fence a masked link is punctuation and a mention is a word.
- **Only known fields are read.** `username`, `content`, `embeds`, `avatar_url` in an incoming body
  are ignored — the message we send is built from scratch, so there is no shape to hijack.
- **Bidi overrides and control characters are stripped** (U+202A–U+202E, U+2066–U+2069, zero-widths,
  C0). A report must not be able to *display* as something other than what it says.
- **No URL from the payload is ever used.** No embed points anywhere but `attachment://`, so the
  relay cannot be aimed at a third-party host, and no report can carry a tracking pixel.
- **Attachments are checked and renamed.** PNG magic bytes must match; the filename and content type
  are ours. A channel that accepts arbitrary bytes under a `.png` is still a file drop.
- **The webhook secret is validated** as a `discord.com` webhook URL before use, so a mistyped
  secret cannot turn the relay into a request forwarder.
- **`application/json` is required**, which forces a preflight on any browser-originated POST, and
  no `OPTIONS` is answered. A random web page cannot use this endpoint.
- **The forum post is named by us**, from the item's own name plus the report id, capped at
  Discord's 100 characters with the id's room reserved first — so a reporter cannot choose what a
  thread in your server is called, and two reports about the same base are still two posts.
- **Discord's own limits are applied here** (6000 per embed, 4096 description, 1024 per field) so an
  oversized report is trimmed by us rather than rejected wholesale by Discord.

What none of this buys: the endpoint is public and anyone who watches the app's traffic will find
it. The protection against abuse is the rate limit, the daily cap and the kill switch — not secrecy.
