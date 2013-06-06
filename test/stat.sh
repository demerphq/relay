for i in `ls -1 result-* | cut -f 2 -d '-' | sort | uniq`; do
	echo `grep -o 'expected [[:digit:]]*' result-$i* -H | sort -u -k 2 -n | tail -3 | tac | sed -s 's/expected //g' | sed -s 's/\.txt//g'`
done