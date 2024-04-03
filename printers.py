class VariantPrinter:
	def __init__(self, value, types):
		self.value = value
		self.types = types

	def children(self):
		id = int(self.value["id"])
		if id == 0:
			return
		else:
			type_name = self.types[id - 1]
			type = gdb.lookup_type(type_name).pointer()
			storage = self.value["storage"].cast(type).dereference()
			yield "[contained value]", storage


class SharedPtrPrinter:
	def __init__(self, value):
		self.value = value

	def children(self):
		yield "[ptr]", self.value["ptr"].dereference()


def print_func(value):
	type = str(value.type.strip_typedefs())
	if type.startswith("qacpi::Variant<"):
		types_str = type[15:-1]
		types = []
		pos = 0
		while pos < len(types_str):
			end = pos
			brackets = 0
			for c in types_str[pos:]:
				if c == "<":
					brackets += 1
				elif c == ">":
					brackets -= 1
				elif c == "," and brackets == 0:
					break
				end += 1

			type_str = types_str[pos:end]
			pos = end + 2
			types.append(type_str)

		return VariantPrinter(value, types)
	elif type.startswith("qacpi::SharedPtr<"):
		return SharedPtrPrinter(value)


gdb.pretty_printers.append(print_func)
