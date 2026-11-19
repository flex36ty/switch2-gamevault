"""Upload workbench artifacts or atomically replace its active test plan."""
import argparse, ftplib, hashlib, io, json
from pathlib import Path

ROOT = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--host', default='192.168.0.112')
parser.add_argument('--port', type=int, default=1337)
parser.add_argument('--plan', type=Path, help='Upload only this plan as active.plan')
args = parser.parse_args()

def retrieve(ftp, remote):
    data = io.BytesIO()
    ftp.retrbinary('RETR ' + remote, data.write)
    return data.getvalue()

def put(ftp, local, remote):
    data = local.read_bytes()
    temporary = remote + '.next'
    ftp.storbinary('STOR ' + temporary, io.BytesIO(data))
    if retrieve(ftp, temporary) != data:
        raise RuntimeError('Upload readback mismatch: ' + temporary)
    # A failed rename leaves the old active file intact. Do not delete it.
    ftp.rename(temporary, remote)
    if retrieve(ftp, remote) != data:
        raise RuntimeError('Final readback mismatch: ' + remote)
    digest = hashlib.sha256(data).hexdigest()
    print(remote, len(data), digest, flush=True)
    return {'path': remote, 'bytes': len(data), 'sha256': digest}

with ftplib.FTP() as ftp:
    ftp.connect(args.host, args.port, timeout=10)
    ftp.login()
    for folder in ('/kaihen', '/kaihen/workbench-plugins', '/kaihen/reload-helper', '/switch'):
        try:
            ftp.cwd(folder)
        except ftplib.error_perm:
            ftp.mkd(folder)
        ftp.cwd('/')
    if args.plan:
        result = [put(ftp, args.plan, '/kaihen/reload-helper/active.plan')]
    else:
        files = [
            (ROOT/'backup-workbench.nso', '/kaihen/workbench-plugins/backup-workbench.nso'),
            (ROOT/'controller/backup-workbench.nro', '/switch/backup-workbench.nro'),
            (ROOT/'scripts/smoke.plan', '/kaihen/reload-helper/active.plan'),
            (ROOT/'scripts/smoke.plan', '/kaihen/reload-helper/smoke.plan'),
            (ROOT/'scripts/post-exit.plan', '/kaihen/reload-helper/post-exit.plan'),
        ]
        result = [put(ftp, local, remote) for local, remote in files]
    (ROOT/'last-upload.json').write_text(json.dumps(result, indent=2))
