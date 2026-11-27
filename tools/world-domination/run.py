#!/usr/bin/env python3
"""Run a Frontier battle on an OpenTS match host and send its lifecycle."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from urllib.error import HTTPError
from urllib.parse import urlencode, urlsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def post(route, fields, operator=False):
    url = os.environ['OPENTS_WDT_API']
    token = os.environ['OPENTS_WDT_OPERATOR_TOKEN' if operator else 'OPENTS_WDT_SERVER_TOKEN']
    endpoint = urlsplit(url)
    if (endpoint.scheme != 'https' and not (
        endpoint.scheme == 'http' and endpoint.hostname in ('localhost', '127.0.0.1', '::1')
    )) or endpoint.query or endpoint.fragment or endpoint.username or len(token) < 32:
        raise ValueError('Configure an HTTPS API URL and a server token of at least 32 characters.')
    request = Request(f'{url}?{urlencode({"route": route})}', json.dumps(fields).encode(),
                      {'Content-Type': 'application/json', 'Authorization': f'Bearer {token}'})
    try:
        with build_opener(NoRedirect()).open(request, timeout=12) as response:
            return json.load(response)
    except HTTPError as error:
        raise RuntimeError(f'Campaign API returned HTTP {error.code} for {route}.') from error


def classify(proto, result_path, battle_id, assigned_map, exit_code):
    if not result_path.is_file() or result_path.stat().st_size > 1024 * 1024:
        return 'aborted', 'OpenTS exited without a readable result.'
    result = proto.Result.FromString(result_path.read_bytes())
    if result.request_id != battle_id or result.code != proto.RESULT_CODE_OK or exit_code != 0:
        return 'aborted', 'OpenTS refused the launch or the match ended with an engine error.'
    if len(result.scenarios) != 1:
        return 'aborted', 'OpenTS did not report exactly one scenario.'
    scenario = result.scenarios[0]
    if Path(scenario.map_path.replace('\\', '/')).name.upper() != assigned_map:
        return 'aborted', 'OpenTS reported a different map.'
    if scenario.outcome == proto.GAME_OUTCOME_CONNECTION_LOST:
        return 'aborted', 'The match lost its connection.'
    if scenario.outcome == proto.GAME_OUTCOME_DESYNC:
        return 'aborted', 'The match went out of sync.'
    if scenario.completed and scenario.outcome in (proto.GAME_OUTCOME_WON, proto.GAME_OUTCOME_LOST, proto.GAME_OUTCOME_DRAW):
        return 'review', 'The local game ended. Verify both players before confirming the outcome.'
    return 'aborted', 'The match did not start or ended without a decision.'


def run(args):
    from google.protobuf.message import DecodeError
    import launch_pb2 as proto

    if args.request.stat().st_size > 1024 * 1024:
        raise ValueError('The launch request exceeds 1 MiB.')
    template = proto.Request.FromString(args.request.read_bytes())
    players = template.play.game.players
    if (template.version != 1 or template.WhichOneof('verb') != 'play'
            or template.play.game.kind != proto.GAME_KIND_MULTIPLAYER
            or not template.play.game.HasField('seed') or not template.play.HasField('network')
            or not template.play.session.HasField('local_player') or not template.play.session.host
            or template.play.session.local_player not in (0, 1)
            or len(players) != 2 or any(not player.human or player.observer for player in players)):
        raise ValueError('Supply a seeded OpenTS multiplayer host request for two human players.')
    args.profile.mkdir(parents=True, exist_ok=True)

    with args.roster.open(encoding='utf-8') as source:
        roster = json.load(source)
    if sorted(player.name for player in players) != sorted(seat['name'] for seat in roster.values()):
        raise ValueError('The host request players must match the invite roster.')
    battle = post('battles', {'territory': args.territory, 'players': roster})
    battle_id, assigned_map = battle['id'], battle['map']
    print(f'Battle {battle_id} reserved on {assigned_map}.', flush=True)
    route = f'battles/{battle_id}'
    body = {'map': assigned_map}
    status, reason = 'aborted', 'The launch runner stopped before receiving a result.'
    try:
        descriptor = os.open(args.invites_out, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, 'w', encoding='utf-8') as output:
            json.dump({'battle': battle_id, 'map': assigned_map, 'invites': battle['invites']}, output)
        with tempfile.TemporaryDirectory(prefix='opents-wdt-') as temp:
            launch_path = Path(temp) / 'launch.bin'
            result_path = Path(temp) / 'result.bin'
            map_path = (args.maps / assigned_map).resolve(strict=True)
            template.play.map_path = str(map_path)
            template.result_path = str(result_path)
            template.request_id = battle_id
            launch_path.write_bytes(template.SerializeToString())
            engine = args.engine.resolve()
            command = [str(engine), f'-LAUNCH={launch_path}', f'-USERDIR={args.profile.resolve()}']
            if args.data_dir:
                command.append(f'-DATADIR={args.data_dir.resolve()}')
            with subprocess.Popen(command, cwd=engine.parent) as process:
                try:
                    post(f'{route}/start', body)
                    exit_code = process.wait(timeout=max(1, battle['expires_at'] - time.time()))
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.wait()
            status, reason = classify(proto, result_path, battle_id, assigned_map, exit_code)
    except (OSError, DecodeError, subprocess.TimeoutExpired) as error:
        print(f'Battle {battle_id}: {error}', file=sys.stderr)
        reason = 'OpenTS failed to launch, timed out, or returned invalid data.'
    finally:
        action = 'review' if status == 'review' else 'result'
        post(f'{route}/{action}', body | {'status': status, 'reason': reason})
        print(f'Battle {battle_id}: {status}. {reason}', flush=True)
    return int(status == 'aborted')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    launch = commands.add_parser('run')
    launch.add_argument('territory')
    for name in ('request', 'engine', 'maps', 'profile', 'data-dir', 'roster', 'invites-out'):
        launch.add_argument(f'--{name}', type=Path, required=name != 'data-dir')
    joined = commands.add_parser('joined', help='Record a seat whose actual game connection the host confirmed.')
    joined.add_argument('battle')
    joined.add_argument('faction', choices=('GDI', 'Nod'))
    joined.add_argument('player')
    resolve = commands.add_parser('resolve', help='Record an independently confirmed decision.')
    resolve.add_argument('battle')
    resolve.add_argument('map')
    resolve.add_argument('outcome', choices=('GDI', 'Nod', 'draw', 'aborted'))
    resolve.add_argument('--reason', required=True)
    args = parser.parse_args()
    if args.command == 'run':
        return run(args)
    if len(args.battle) != 32 or any(char not in '0123456789abcdef' for char in args.battle):
        parser.error('Invalid battle ID.')
    if args.command == 'joined':
        print(json.dumps(post(f'battles/{args.battle}/joined', {'faction': args.faction, 'player': args.player})))
        return 0
    body = {'map': args.map, 'status': args.outcome, 'reason': args.reason,
            'decision': 'cancel' if args.outcome == 'aborted' else 'approve'}
    if args.outcome in ('GDI', 'Nod'):
        body.update(status='won', winner=args.outcome)
    print(json.dumps(post(f'battles/{args.battle}/decision', body, operator=True)))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, KeyError) as error:
        sys.exit(str(error))
