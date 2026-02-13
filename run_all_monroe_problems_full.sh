# Usage: ./compute_manual <domain_file> <problem_file> <observation_file> <num_obs> <k_iterations> <save_dir>

set -e  # Exit on error



# get every name in benchmarks/monroe-100/01-problems in a list
problem_files=$(ls benchmarks/monroe-100/01-problems/*.hddl)

counter=0
domain_file="benchmarks/monroe-100/00-domain/domain.hddl"
for problem_file in $problem_files; do
    # continue if counter equals 0
    if [ $counter -le 29 ]; then
        echo "Processed $counter problem files. Skipping to avoid duplicate processing."
        counter=$((counter + 1))
        continue
    fi

    echo "Processing problem file: $problem_file"
    # Extract the problem name from the file path (p-0006-clear-road-wreck.hddl -> 06)
    problem_id=$(basename "$problem_file" .hddl | cut -d'-' -f2)
    echo "Extracted problem ID: $problem_id"
    # Run the compute_manual executable with the current problem file
    obs_file="benchmarks/monroe-100/02-solutions/solution-${problem_id}.txt"
    num_obs_str="3"
    k_iterations_str="5"
    save_dir="monroe_full_${problem_id}_${num_obs_str}_${k_iterations_str}"

    num_obs=$(grep -o '([^)]*)' "$obs_file" | wc -l)

    for i in $(seq 0 $((num_obs - 1))); do
        echo "Processing observation $i for problem ${problem_id}"
        # Run the compute_manual executable with the current observation
        ./compute_monroe "$domain_file" "$problem_file" "$obs_file" $i "$k_iterations_str" "$save_dir/obs_$i"
    done
    counter=$((counter + 1))

    # clean up memory caches after each problem file to avoid memory issues
    # sudo purge

done

