
#
# Library for interfacing with TriPoint over I2C using the
# FTDI FT2232H and libmpsse library.
#


import mpsse

# I2C Commands
CMD_INFO             = 0x01
CMD_CONFIG           = 0x02
CMD_READ_INTERRUPT   = 0x03
CMD_DO_RANGE         = 0x04
CMD_SLEEP            = 0x05
CMD_RESUME           = 0x06
CMD_SET_LOCATION     = 0x07
CMD_READ_CALIBRATION = 0x08


class TriPoint:

	def __init__ (self):
		self.tripoint = mpsse.MPSSE(mpsse.I2C, mpsse.FOUR_HUNDRED_KHZ)
		self.tripoint.Start()


	def checkAlive (self):
		'''
		Make sure we can communicate with the TriPoint. Returns the version
		on success, and raises an exception on error.
		'''
		self.write_command(CMD_INFO)
		data = self.read_bytes(3)
		fields = struct.unpack('<HB')
		if fields[0] == 0xB01A:
			# Got the correct ID bytes, return the version
			return fields[1]
		else:
			# Something didn't work, raise exception
			raise Exception('Could not talk to TriPoint')


	def write_command (self, cmd):
		out = struct.pack('B', cmd)
		self.tripoint.Write(out)

	def read_bytes (self, len):
		data = self.tripoint.Read(len)
		return data
