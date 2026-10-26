# KubeSched

A Kubernetes-style pod scheduler simulator. It places pods with CPU and memory requests onto heterogeneous nodes using a Filter→Score pipeline, supports bin-packing, spreading and balanced policies with optional priority-based preemption, and compares those policies across generated workload patterns.

```
make && make test
./kubesched run --policy binpack --preempt < input.txt
./kubesched eval
```

Input format (`#` starts a comment):

```
node <name> <cpu_millicores> <mem_mib> [max_pods]
pod  <name> <cpu_millicores> <mem_mib> <priority> <arrival> <duration>
```

See [PROGRESS.md](PROGRESS.md) for the design, the plan and progress, and the evaluation results.
