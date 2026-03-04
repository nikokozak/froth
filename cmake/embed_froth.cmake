file(READ "${INPUT}" hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, " c_bytes "${hex}")
file(WRITE "${OUTPUT}" "static const char ${VARNAME}[] = {${c_bytes}0x00};\n")

