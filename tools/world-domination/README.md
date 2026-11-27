# World Domination pilot

Frontier is a single theater with three fixed two-player scenarios. The public
endpoint lists the territories, current owner, assigned scenario, and any
unexpired battle. Initial ownership is GDI, Neutral, and Nod.

Deploy `public/index.php` under the existing PHP site. Use PHP 8.1 or later
with PDO SQLite. Give the PHP service a writable database directory outside
the web root. Set `OPENTS_WDT_DATABASE` to its absolute SQLite filename and
`OPENTS_WDT_SERVER_TOKEN` to a random server credential of at least 32
characters. Use HTTPS for remote server requests.

| Request | Body | Result |
| --- | --- | --- |
| GET `?route=theater` | — | Theater snapshot |
| POST `?route=battles` | `{"territory":"central-basin"}` | Battle ID, assigned map, and expiry |
| POST `?route=battles/<id>/result` | `{"status":"won","winner":"Nod","map":"WDTBASIN.MPR"}` | Recorded outcome |

The match service sends its credential as `Authorization: Bearer <token>` for
both POST routes. Keep that credential on the server. The service uses the
reserved filename as `map_path` in the existing OpenTS launch request and
retains the battle ID for its verified result. Distribute the scenario files
from `maps/` to the machines that run the match. Report a draw or an abort
with the assigned map and without a winner. A reserved battle expires after
two hours. A win updates ownership atomically, and retrying the same result
returns the original outcome. Draws, aborts, and expired battles leave
ownership unchanged.

The existing match service supplies the lobby, roster, launch, and verified
result. A client's local launch result alone cannot confirm a territory win.
The in-game menu and public map can use the theater endpoint once the service
is integrated.


## Match-host runner

The runner uses the MariaDB website API in [public-web](../../public-web/README.md).
Use the match host's normal two-player OpenTS multiplayer launch request as the
input. The match host still supplies the peer connection details, starts the
remote client with the same assigned map and game settings, and verifies the
two players' result before approval. A transport-only relay can continue
handling traffic, with the runner on a machine capable of launching OpenTS.

Generate Python bindings from the existing launch schema with protoc and make
them available on `PYTHONPATH`. The runner requires Python 3.9 or newer and
a Python protobuf runtime compatible with the installed protoc.

```sh
mkdir -p /tmp/opents-launch-proto
protoc -I docs/launch --python_out=/tmp/opents-launch-proto docs/launch/launch.proto
export PYTHONPATH=/tmp/opents-launch-proto
export OPENTS_WDT_API=https://your-opents-site.example/api.php
# Set OPENTS_WDT_SERVER_TOKEN in the runner's environment.

python3 tools/world-domination/run.py run central-basin \
  --request /path/to/host-request.bin --engine /path/to/OpenTS \
  --maps tools/world-domination/maps --profile /path/to/isolated-host-profile \
  --roster /path/to/roster.json --invites-out /private/path/to/new-invites.json
```

The roster file is keyed by faction. Use the exact names in the host request.

```json
{
  "GDI": {"name": "Alice", "connection": {"host": "match.example", "port": 5001}},
  "Nod": {"name": "Bob", "connection": {"host": "match.example", "port": 5002}}
}
```

Distribute the generated invite codes privately. The output file must be new
and should be removed after delivery. Once the match host confirms a checked-in
player actually connected, report it with `run.py joined <battle-id> GDI Alice`.

Add `--data-dir` if the engine's game data are outside its configured game
data location. Keep the server token on the match service. Each concurrent
engine process needs its own profile and network port.

The runner reserves the territory, launches the assigned map, marks the
battle In Progress, and reads the local result. Refusals, crashes, known
disconnects, and desyncs abort the battle and release the territory. A local
win or loss goes to Awaiting Review. A peer dropping out can also produce a
local win, so the runner leaves ownership unchanged in that case.

Once the operator verifies both players and the outcome, use the operator
queue on the website or run the command below with
`OPENTS_WDT_OPERATOR_TOKEN` in the operator's environment:

```sh
python3 tools/world-domination/run.py resolve <battle-id> WDTBASIN.MPR Nod \
  --reason "Pilot operator confirmed the outcome with both players"
```

Use `draw` or `aborted` instead of a faction to close without changing
ownership. The reason is shown in the public match history.
