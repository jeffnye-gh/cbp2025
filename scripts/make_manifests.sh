for g in compress fp infra int media web; do
  find traces/$g -name '*_trace.gz' | sort > manifests/$g.txt
done
