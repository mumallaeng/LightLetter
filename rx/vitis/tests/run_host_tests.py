from pathlib import Path
import subprocess, shutil, tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='lightletter_rx_tests_') as temp:
    out=Path(temp);src=root/'vitis/src'
    exe=out/'rx_test.exe'
    sources=[root/'tests/firmware/test_dashboard.c',src/'drv/drv_bfsk_rx.c',src/'drv/drv_fft_snapshot.c',src/'app/app_dashboard.c']
    subprocess.run([shutil.which('gcc'),'-std=c11','-Wall','-Wextra','-Werror','-I'+str(src),*[str(x) for x in sources],'-o',str(exe)],check=True)
    result=subprocess.run([str(exe)],capture_output=True,check=True)
    fixture=out/'output.jsonl';fixture.write_bytes(result.stdout)
    print(result.stderr.decode())
    subprocess.run(['node',str(root/'tests/firmware/test_protocol.cjs'),str(fixture),str(root/'ui/protocol.js')],check=True)
    subprocess.run(['node','--check',str(root/'ui/app.js')],check=True)
