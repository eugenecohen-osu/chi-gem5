# todo: add copyright

from m5.params import *
from m5.proxy import *
#from m5.SimObject import SimObject
from m5.objects.Process import EmulatedDriver

class AlaskaDriver(EmulatedDriver):
    type = "AlaskaDriver"
    cxx_class = "gem5::AlaskaDriver"
    cxx_header = "dev/alaska/alaska_driver.hh"
