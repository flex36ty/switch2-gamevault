from pathlib import Path
import ftplib,io,hashlib,json,datetime
out=Path('investigation/reloadable-helper/no-restart-check')/datetime.datetime.now().strftime('%Y%m%d-%H%M%S');out.mkdir(parents=True)
with ftplib.FTP() as f:
 f.connect('192.168.0.112',1337,timeout=10);f.login()
 for name in ['/kaihen/workbench-status.txt','/kaihen/reload-helper/run.log','/kaihen/reload-helper/active.plan']:
  b=io.BytesIO()
  try:f.retrbinary('RETR '+name,b.write)
  except ftplib.all_errors as e:print(name,str(e));continue
  data=b.getvalue();(out/Path(name).name).write_bytes(data);print(name, data.decode(errors='replace')[-2500:])
 names=f.nlst('/kaihen/workbench-plugins');print('Plugins',names)
 for name in names:
  if not name.endswith('.nso'):continue
  if not name.startswith('/'):name='/kaihen/workbench-plugins/'+name
  b=io.BytesIO();f.retrbinary('RETR '+name,b.write);data=b.getvalue();(out/Path(name).name).write_bytes(data);print('Plugin SHA256',hashlib.sha256(data).hexdigest())
print('Saved:',out)
