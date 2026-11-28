# Enhanced OpenTS public website

The public site has Overview, Features, Downloads, Source and Sign in pages.
Downloads reads the public GitHub releases for `retrocs-org/enhanced-opents`.
Name binaries with their platform (macOS, Windows, Win32, Win64 or Linux)
and include `server` or `dedicated` in Linux dedicated-server asset names.
PHP needs outbound HTTPS and `allow_url_fopen` for the release feed. APCu
caches results when available.
Command Uplink previews live campaign control, active battles and recent
results, with links to Frontier and the Public Server Browser.
The Frontier page reads live territory owners, active battles, confirmed
wins and recent results from MariaDB. Public Server Listing and Leaderboards
are unavailable until a public directory and match results are connected.
The invite-only check-in remains available at `lobby.php` for match hosts
to share directly. Players manage their website accounts on the Sign in page.

## Setup

1. Create the MariaDB database and an application account with SELECT,
   INSERT and UPDATE on that database. Grant CREATE while initializing the
   schema.
2. Point the virtual host at this directory. Use PHP 8.1 or newer with
   PDO MySQL. Copy `config.inc.php.example` to `config.inc.php` and fill in
   the database password, operator name and two distinct random tokens of
   at least 32 characters. Give the PHP worker read access to the file.
   Environment variables with the same names override the file settings.
3. Run `php migrate.php` from the deployed document root as the web
   service user. The command creates missing tables and inserts missing
   territories. Revoke CREATE after it succeeds. For a pilot database,
   follow the upgrade steps below.
4. Open `index.php` for the site or `api.php?route=theater` for JSON.
   For local development, run `php -S 127.0.0.1:8080 -t public-web`
   from the project root. Serve the live site over HTTPS.

## Player accounts

On an existing installation, apply `migrations/005-player-accounts.sql` to
the website database as an administrator. On a fresh installation,
`php migrate.php` creates the account tables. Grant the website account
DELETE on `opents_account_rate` so it can remove expired rate-limit entries.
The usual SELECT, INSERT, and UPDATE grants cover the rest of account access.

Set `OPENTS_ACCOUNT_ORIGIN` to the public HTTPS origin and
`OPENTS_ACCOUNT_MAIL_FROM` to a sender accepted by the mail relay.
PHP `mail()` must have a working sendmail transport on the web host.
Serve the site over HTTPS for secure session cookies. Registration emails
a link for setting the first password. Recovery emails a single-use
password reset link and invalidates existing player sessions when used.
Player accounts are for this website.

## Frontier challenges

Central Basin borders all four outer sectors. Northern Pass also borders
River Crossing, and Southern Flats also borders Ridge Line. A faction
can challenge a territory it does not own if it owns a neighboring territory.
A faction with no territory can challenge its original home territory
(River Crossing for GDI, Ridge Line for Nod). Each battle still has one seat
per faction. The host may only reserve a territory that has a challenger
and no active reservation. The public snapshot lists eligible challengers
alongside the battle status, and the map shows the active battle first.

For an existing Frontier database, apply
`migrations/004-frontier-sectors.sql` to add the two neutral sectors. It
can be applied again without resetting ownership or battle history.
Northern Pass uses the Ridge Line scenario. Southern Flats uses the
Central Basin scenario. A fresh database already includes both sectors.

## Battle lifecycle

For an existing pilot database, pause campaign writes and apply
`migrations/002-battle-lifecycle.sql` once before deploying the new PHP files.
The migration converts currently active rows to Reserved. Then apply
`migrations/003-operator-lobby.sql`. On a new database, `schema.sql` includes
both changes.

| Request | Body |
| --- | --- |
| GET `api.php?route=theater` | Returns territory ownership, open battles, and recent decisions |
| POST `api.php?route=battles` | Territory and two faction-keyed seats. Each seat has a player name and connection host and port. |
| POST `api.php?route=battles/<id>/start` | `{"map":"WDTBASIN.MPR"}` |
| POST `api.php?route=battles/<id>/review` | Assigned `map` and a short `reason` |
| POST `api.php?route=battles/<id>/result` | Runner abort with assigned `map`, `status: "aborted"` and `reason` |
| POST `api.php?route=battles/<id>/joined` | Exact roster `player` and `faction`, after the host confirms the game connection |
| POST `api.php?route=battles/<id>/decision` | Operator-only `decision`, assigned `map`, final `status`, `reason`, and `winner` for a win |

Use the runner bearer token for runner routes and the operator bearer token
for decisions. The operator can also sign into `operator.php` using the
configured name and operator token. Approve, reject, and cancel decisions each create an audit record. Only an
operator-confirmed win changes ownership. Use a public-facing reason.
Reserved, In Progress, and Awaiting Review each hold the territory for the
same two-hour reservation. A failed launch can abort directly from Reserved.
See the [match-host runner](../tools/world-domination/README.md#match-host-runner)
for launch and outcome handling.

Give each player only their own invite code through the match host's private
channel. Players enter it at `lobby.php`, which records check-in. The match
host separately records actual game connections through the joined route.

Set `OPENTS_WDT_API` to the site's HTTPS `api.php` URL in the game
environment to enable the **Frontier** menu. It shows the
territories and recent results. The game uses the system curl
executable for this public read. The private web lobby handles check-in.
