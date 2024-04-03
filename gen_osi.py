#!/usr/bin/env python

import sys


def main():
	src_name = sys.argv[1]
	dest_name = sys.argv[2]

	with open(src_name, "rb") as file:
		data = file.read()
		size = len(data) - 36
		data = data[36:]

		# MethodOp
		data = data[1:]

		first = data[0]
		count = first >> 6
		data = data[count + 1:]

		# skip name and flags
		data = data[5:]

		cpp = "#pragma once\n#include <stdint.h>\n\nnamespace qacpi {\n\tconstexpr uint8_t OSI_DATA[] {\n\t\t"

		count_in_line = 0
		for i in range(len(data)):
			byte = data[i]
			if count_in_line == 20:
				cpp += "\n\t\t"
				count_in_line = 0

			cpp += "0x{:X}".format(byte)
			if i != len(data) - 1:
				cpp += ","
			count_in_line += 1

		cpp += "\n\t};\n\n"
		cpp += "\tconstexpr uint32_t OSI_SIZE = {};\n}}".format(len(data))

		with open(dest_name, "w") as dest:
			dest.write(cpp)


if __name__ == "__main__":
	main()
