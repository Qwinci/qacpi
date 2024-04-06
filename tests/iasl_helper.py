#!/usr/bin/env python

import sys
import subprocess

def main():
	args = sys.argv[1:]
	ret = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
	stdout = ret.stdout.decode("utf-8")
	stderr = ret.stderr.decode("utf-8")

	print(stdout)
	print(stderr)

	if "0 Errors" not in stdout:
		exit(1)

if __name__ == "__main__":
	main()
