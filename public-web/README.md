# OpenTS Enhanced public website

The site shows Frontier territory ownership, active reservations and recent
battles. It also links to OpenTS releases and exposes the campaign API.

## Setup

1. Create a MariaDB database and import `schema.sql` with an account that
   can create tables. Run the site with a separate account that can select,
   insert and update rows in the campaign tables.
2. Point the PHP virtual host at this directory. Use PHP 8.1 or newer with
   PDO MySQL. Supply these environment variables to the PHP worker.

       OPENTS_WEB_DSN=mysql:host=127.0.0.1;dbname=opents;charset=utf8mb4
       OPENTS_WEB_DB_USER=opents_web
       OPENTS_WEB_DB_PASSWORD=<database password>
       OPENTS_WDT_SERVER_TOKEN=<random server credential of at least 32 characters>
       OPENTS_WDT_OPERATOR_NAME=<operator username>
       OPENTS_WDT_OPERATOR_TOKEN=<different random credential of at least 32 characters>

3. Open `index.php` for the site or `api.php?route=theater` for JSON.
   For local development, run `php -S 127.0.0.1:8080 -t public-web`
   from the project root. Serve the live site over HTTPS and keep credentials
   in the PHP worker environment.

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
environment to enable the **World Domination Tour** menu. It shows the
three territories and recent results. The game uses the system curl
executable for this public read. The private web lobby handles check-in.
