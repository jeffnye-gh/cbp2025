
#./bin/cbp.tage192 traces/sml/454.calculix-104B.champsimtrace.xz
#./bin/cbp.tage192 traces/sml/int_13_trace.xz

INC=traces/sml/454.calculix-104B.champsimtrace.xz
OUTC=champsim.txt
INB=traces/sml/int_13_trace.gz
OUTB=cbp.txt
python3 scripts/convert_trace.py -i ${INC} -o ${OUTC} -n 1000
python3 scripts/convert_trace.py -i ${INB} -o ${OUTB} -n 1000
