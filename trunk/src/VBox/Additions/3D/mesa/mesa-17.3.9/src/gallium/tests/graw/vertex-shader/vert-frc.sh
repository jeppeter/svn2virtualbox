VERT

DCL IN[0]
DCL OUT[0], POSITION
DCL OUT[1], COLOR

DCL TEMP[0]

IMM FLT32 { 2.7, 3.1, 4.5, 1.0 }

MUL TEMP[0], IN[0].xyxw, IMM[0]
MOV OUT[0], IN[0]
FRC OUT[1], TEMP[0]

END
