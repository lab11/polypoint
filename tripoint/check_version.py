import tripoint



import time
version = 0

tp = tripoint.TriPoint()

version = tp.checkAlive()

print('Got version: {}'.format(version))
time.sleep(1)

tp.close()




