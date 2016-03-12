#ifndef _LINUX_OOMCONTROL_H
#define _LINUX_OOMCONTROL_H

#ifdef CONFIG_CGROUP_OOM

struct oom_cgroup { 
	struct cgroup_subsys_state css;

	/*
	 * the order to be victimized for this group
	 */  
	atomic_t priority;

	/*
	 * the maximum priority along the path from root
	 */  
	atomic_t effective_priority;

};

/*
 * disable during cpuset constrained oom
 */
extern atomic_t honour_cpuset_constraint;

u64 task_oom_priority(struct task_struct *p);

#else

#define task_oom_priority(p) (1)

static atomic_t honour_cpuset_constraint; /* unused */

#endif
#endif
