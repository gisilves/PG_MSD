
for run in $(seq $1 $2); do
	./scripts/analyzeRun.sh -j json/ev.json -f $run --no-compile;# ./scripts/beamReports.sh -f $run -l $run;
done
