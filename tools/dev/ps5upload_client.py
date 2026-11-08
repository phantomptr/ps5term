"""Small, fail-closed ps5upload development client. GPL-3.0-or-later."""
from __future__ import annotations
import json
import base64
import re
import urllib.parse
import urllib.request
from .console_target import require_phat


class ApiError(RuntimeError):
    pass


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise ApiError('Engine redirects are refused')


class Ps5UploadClient:
    # An allowlist prevents reaching power, kernel, loader or generic launch routes.
    ROUTES = {
        ('GET', '/api/ps5/hw/info'), ('GET', '/api/ps5/process/list'),
        ('GET', '/api/ps5/focus'), ('POST', '/api/ps5/fs/read-preview'),
        ('POST', '/api/transfer/dir'), ('POST', '/api/ps5/fs/delete'),
        ('GET', '/api/ps5/list-dir'), ('POST', '/api/ps5/process/kill'),
        ('POST', '/api/ps5/fs/write-bytes'), ('POST', '/api/ps5/fs/chmod'),
    }

    def __init__(self, addr: str, engine='http://127.0.0.1:19113', timeout=15):
        self.addr = require_phat(addr)
        parsed = urllib.parse.urlsplit(engine)
        if (parsed.scheme != 'http' or parsed.hostname != '127.0.0.1' or
                parsed.username or parsed.password or parsed.path not in ('', '/') or
                parsed.query or parsed.fragment):
            raise ValueError('Engine must be an explicit loopback HTTP URL')
        self.engine, self.timeout = engine.rstrip('/'), timeout

    def request(self, method: str, path: str, body: dict | None = None) -> dict:
        require_phat(self.addr)
        parsed = urllib.parse.urlsplit(path)
        if parsed.scheme or parsed.netloc or parsed.fragment:
            raise ValueError('Only relative engine API routes are accepted')
        query = urllib.parse.parse_qs(parsed.query, keep_blank_values=True)
        if 'addr' in query and query['addr'] != [self.addr]:
            raise ValueError('Target override refused')
        if body is not None and 'addr' in body and body['addr'] != self.addr:
            raise ValueError('Target override refused')
        job = bool(re.fullmatch(r'/api/jobs/[A-Za-z0-9_-]{1,128}', parsed.path))
        if (method, parsed.path) not in self.ROUTES and not (method == 'GET' and job):
            raise ApiError('API route not approved for PS5Term development')
        if method == 'POST' and parsed.path == '/api/ps5/fs/write-bytes':
            allowed = {'/data/ps5term/diagnostics.enabled': b'1\n',
                       '/data/ps5term/probe.request': b'baseline-v1\n',
                       '/data/ps5term/child-probe.request': b'child-v1\n'}
            try: payload = base64.b64decode((body or {}).get('bytes_b64', ''), validate=True)
            except (ValueError, TypeError): raise ApiError('Invalid diagnostic write') from None
            if (not body or body.get('path') not in allowed or
                    payload != allowed[body['path']] or body.get('create_only', False) is not False):
                raise ApiError('Only fixed diagnostic requests may be written')
        if method == 'POST' and parsed.path == '/api/ps5/fs/chmod':
            if (not body or body.get('path') not in ('/data/ps5term/status.json', '/data/ps5term/capabilities.json',
                                                   '/data/ps5term/child-capabilities.json') or
                    body.get('mode') != '0644' or body.get('recursive', False) is not False):
                raise ApiError('Only secret-free diagnostic reports may be made readable')
        if method == 'POST' and parsed.path == '/api/ps5/process/kill':
            pid = (body or {}).get('pid')
            if type(pid) is not int or pid <= 1: raise ApiError('Invalid app PID')
            processes = self.request('GET', '/api/ps5/process/list')
            owned = [p for p in processes.get('processes', []) if p.get('pid') == pid]
            if processes.get('truncated') or len(owned) != 1 or owned[0].get('title_id') != 'FAKE99260':
                raise ApiError('Process is not the verified PS5Term app')
        # Even engine-local job polling carries the explicit target; the engine ignores it.
        if method == 'GET':
            query['addr'] = [self.addr]
        else:
            body = dict(body or {}, addr=self.addr)
        url = self.engine + parsed.path
        if query: url += '?' + urllib.parse.urlencode(query, doseq=True)
        data = None if body is None else json.dumps(body).encode()
        request = urllib.request.Request(url, data=data, method=method,
                                         headers={'Content-Type': 'application/json'})
        opener = urllib.request.build_opener(NoRedirect)
        with opener.open(request, timeout=self.timeout) as response:
            raw = response.read(2 * 1024 * 1024 + 1)
        if len(raw) > 2 * 1024 * 1024: raise ApiError('Engine response exceeds bound')
        result = json.loads(raw)
        if not isinstance(result, dict): raise ApiError('Engine response must be an object')
        if result.get('error') or result.get('ok') is False:
            raise ApiError('Engine reported failure')
        return result
