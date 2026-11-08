"""Collect secret-free app evidence from independent ps5upload sources.
GPL-3.0-or-later. Never launch; caller holds hardware_cycle.TestWindow.
"""
import base64
from .console_target import require_phat
from .hardware_cycle import verify_readiness
from .ps5upload_client import ApiError
import json


def read_report(client, name):
    require_phat(client.addr)
    if name not in ('status.json', 'capabilities.json', 'child-capabilities.json'):
        raise ValueError('Only secret-free app reports are accepted')
    path = '/data/ps5term/' + name
    # Native app UID differs from the helper. No credentials/history are included.
    client.request('POST', '/api/ps5/fs/chmod', dict(path=path, mode='0644', recursive=False))
    result = client.request('POST', '/api/ps5/fs/read-preview', dict(path=path, offset=0, max_bytes=4097))
    data = base64.b64decode(result['base64'], validate=True)
    if len(data) > 4096 or result.get('size') != len(data):
        raise ApiError('Diagnostic report exceeds bound or length mismatches')
    report = json.loads(data)
    if not isinstance(report, dict): raise ApiError('Diagnostic report must be an object')
    return report


def capture_child_experiment(client, readiness):
    report = read_report(client, 'child-capabilities.json')
    results = report.get('results')
    if (report.get('schema') != 1 or report.get('context') != 'app' or
            report.get('scope') != 'child-experiment' or
            report.get('build_id') != readiness['status']['build_id'] or
            type(report.get('complete')) is not bool or type(report.get('cleanup_ok')) is not bool or
            not isinstance(results, list) or len(results) != 7 or
            any(not isinstance(r, dict) or r.get('pid') != readiness['status']['pid'] for r in results)):
        raise ApiError('Child report does not match verified app context')
    # Preserve negative/incomplete results as evidence; never reinterpret them as
    # supported just because HTTP or child creation succeeded.
    return report


def capture_readiness(client, expected_build, previous_instance=None):
    status = read_report(client, 'status.json')
    process_list = client.request('GET', '/api/ps5/process/list')
    if process_list.get('truncated'): raise ApiError('Process list is truncated')
    apps = [p for p in process_list.get('processes', []) if p.get('title_id') == 'FAKE99260']
    focus = client.request('GET', '/api/ps5/focus')
    status = dict(status, foreground=(len(apps) == 1 and apps[0].get('pid') == status.get('pid')
        and type(apps[0].get('app_id')) is int and apps[0]['app_id'] > 0
        and apps[0]['app_id'] == focus.get('focus_app_id')))
    if not verify_readiness(status, expected_build, previous_instance):
        raise ApiError('Build, instance, live title/PID or foreground verification failed')
    return dict(schema=1, addr=client.addr, status=status, app=apps[0],
                focus_app_id=focus['focus_app_id'], readiness_verified=True)


def capture_capabilities(client, readiness):
    report = read_report(client, 'capabilities.json')
    results = report.get('results')
    if (report.get('schema') != 1 or report.get('context') != 'app' or
            report.get('scope') != 'baseline-only' or
            report.get('build_id') != readiness['status']['build_id'] or
            report.get('cleanup_ok') is not True or not isinstance(results, list) or len(results) != 7 or
            any(not isinstance(r, dict) or r.get('pid') != readiness['status']['pid'] for r in results)):
        raise ApiError('Capability report does not match the verified app context')
    return report
