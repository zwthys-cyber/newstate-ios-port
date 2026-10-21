"""Regression: a corrupted signed code page must fail the artifact gate."""
import pathlib
import subprocess
import sys
import tempfile

source=pathlib.Path(sys.argv[1])
verify=pathlib.Path(__file__).resolve().parents[1]/'scripts/verify_artifact.py'
subprocess.run([sys.executable,str(verify),str(source),'@rpath/NewStateLoadProbe.dylib'],check=True,stdout=subprocess.DEVNULL)
with tempfile.TemporaryDirectory() as folder:
    corrupted=pathlib.Path(folder)/source.name
    data=bytearray(source.read_bytes())
    data[4096]^=1
    corrupted.write_bytes(data)
    result=subprocess.run([sys.executable,str(verify),str(corrupted),'@rpath/NewStateLoadProbe.dylib'],capture_output=True,text=True)
    assert result.returncode!=0 and 'signature page hash mismatch' in result.stderr,result.stderr
print('PASS: valid signed artifact accepted; changed code page rejected')
