To run experiments for the kitchen domain, use the following commands:

Make sure to compile the following software components:
* PANDA parser
* PANDA grounder
* PANDA planner
* PANDA PIpgrRepairVerify
* Code in this repository


```bash
./compute_posterior.sh <domain_file> <problem_file> <observation_file> <num_obs> <k_iterations>  # for individual kitchen problems
```

```bash
./run_all_kitchen_problems_full.sh    # run all kitchen problems with full observation
```

```bash
./run_all_kitchen_problems_partial.sh # run all kitchen problems with partial observation
```

```bash
./run_monroe_problems_full.sh         # run the Monroe problems in the kitchen domain with full observation
```
