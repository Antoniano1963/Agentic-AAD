import sys,os,traceback,gc
from pathlib import Path
bridge=sys.argv[1]
code=compile(Path(bridge).read_text(),bridge,'exec')
basepath=list(sys.path)
for request in sys.stdin:
    try:
        a,b=request.rstrip('\n').split('\t')
        sys.argv=[bridge,a,b]
        namespace={'__name__':'__main__','__file__':bridge}
        exec(code,namespace)
        namespace.clear()
        gc.collect()
        sys.path[:]=basepath
        print('OK',flush=True)
    except BaseException:
        traceback.print_exc(file=sys.stderr)
        print('ERROR',flush=True)
        break
