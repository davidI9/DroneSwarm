import sys
if sys.prefix == '/home/david/code/prueba-ros/ros2-proy-prueba/.pixi/envs/default':
    sys.real_prefix = sys.prefix
    sys.prefix = sys.exec_prefix = '/home/david/code/prueba-ros/ros2-proy-prueba/install/my_package'
