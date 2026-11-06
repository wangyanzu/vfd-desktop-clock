from pathlib import Path
import os
Import('env')
root=Path(os.environ.get('W4HOME_SERVER_ROOT',str(Path(env.subst('$PROJECT_DIR')).parent/'w4home-server')))
env.Append(CPPPATH=[str(root/'firmware/common')])
