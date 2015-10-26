
#
# Library for interfacing with TriPoint over I2C using the
# FTDI FT2232H and libmpsse library.
#

import struct

import mpsse

# Address
TRIPOINT_ADDRESS = 0x65

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

	def ledsOff (self):
		self.tripoint.PinHigh(mpsse.GPIOL0)

	def checkAlive (self):
		'''
		Make sure we can communicate with the TriPoint. Returns the version
		on success, and raises an exception on error.
		'''
		self.write_command(CMD_INFO)
		data = self.read_bytes(3)
		fields = struct.unpack('<HB', data[0:3])
		if fields[0] == 0x1AB0:
			# Got the correct ID bytes, return the version
			return fields[1]
		else:
			# Something didn't work, raise exception
			raise Exception('Could not talk to TriPoint')

	def close (self):
		self.tripoint.Close()


	def write_command (self, cmd):
		out = struct.pack('BB', TRIPOINT_ADDRESS<<1, cmd)
		self.tripoint.Start()
		self.tripoint.Write(out)
		self.tripoint.Stop()

	def read_bytes (self, len):
		out = struct.pack('B', TRIPOINT_ADDRESS<<1 | 1)
		self.tripoint.Start()
		self.tripoint.Write(out)
		data = self.tripoint.Read(len-1)
		self.tripoint.SendNacks()
		data += self.tripoint.Read(1)
		self.tripoint.Stop()
		return data


