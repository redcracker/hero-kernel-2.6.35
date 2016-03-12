/*
 * kernel/cgroup_oom.c - oom handler cgroup.
 */

#include <linux/cgroup.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/oomcontrol.h>
#include <asm/atomic.h>

atomic_t honour_cpuset_constraint;

/*
 * Helper to retrieve oom controller data from cgroup
 */
static struct oom_cgroup *oom_css_from_cgroup(struct cgroup *cgrp)
{
        return container_of(cgroup_subsys_state(cgrp,
                                oom_subsys_id), struct oom_cgroup,
                                css);
}

u64 task_oom_priority(struct task_struct *p)
{
	rcu_read_lock();
	return atomic_read(&(container_of(task_subsys_state(p,oom_subsys_id),
				struct oom_cgroup, css))->effective_priority);
	rcu_read_unlock();
}

static struct cgroup_subsys_state *oom_create(struct cgroup_subsys *ss,
						   struct cgroup *cont)
{
	struct oom_cgroup *oom_css = kzalloc(sizeof(*oom_css), GFP_KERNEL);
	struct oom_cgroup *parent;
	u64 parent_priority, parent_effective_priority;

	if (!oom_css)
		return ERR_PTR(-ENOMEM);

	/*
	 * if root last/only group to be victimized
	 * else inherit parents value
	 */
	if (cont->parent == NULL) {
		atomic_set(&oom_css->priority, 1);
		atomic_set(&oom_css->effective_priority, 1);
		atomic_set(&honour_cpuset_constraint, 0);
	} else {
		parent = oom_css_from_cgroup(cont->parent);
		parent_priority = atomic_read(&parent->priority);
		parent_effective_priority = 
			atomic_read(&parent->effective_priority);
		atomic_set(&oom_css->priority, parent_priority);
		atomic_set(&oom_css->effective_priority,
					parent_effective_priority);
	}

	return &oom_css->css;
}

static void oom_destroy(struct cgroup_subsys *ss, struct cgroup *cont)
{
	kfree(cont->subsys[oom_subsys_id]);
}

static void increase_effective_priority(struct cgroup *cgrp, u64 val)
{
	struct cgroup *curr;
	struct oom_cgroup *oom_css;

	atomic_set( &(oom_css_from_cgroup(cgrp))->effective_priority, val);

	mutex_lock(&oom_subsys.hierarchy_mutex);

	/*
	 * DFS
	 */
	if (!list_empty(&cgrp->children))
		curr = list_first_entry(&cgrp->children,
					struct cgroup, sibling);
	else
		goto out;

visit_children:
	oom_css = oom_css_from_cgroup(curr);
	if (atomic_read(&oom_css->effective_priority) < val)
		atomic_set(&oom_css->effective_priority, val);

	if (!list_empty(&curr->children)) {
		curr = list_first_entry(&curr->children,
					struct cgroup, sibling);
		goto visit_children;
	} else {
visit_siblings:
		if (curr == 0 || cgrp == curr) goto out;

		if (curr->sibling.next != &curr->parent->children) {
			curr = list_entry(curr->sibling.next,
						struct cgroup, sibling);
			goto visit_children;
		} else {
			curr = curr->parent;
			goto visit_siblings;
		}
	}
out:
	mutex_unlock(&oom_subsys.hierarchy_mutex);

}

static void decrease_effective_priority(struct cgroup *cgrp, u64 val)
{
	struct cgroup *curr;
	u64 priority, effective_priority;


	effective_priority = val;

	atomic_set(&oom_css_from_cgroup(cgrp)->effective_priority,
							effective_priority);

	mutex_lock(&oom_subsys.hierarchy_mutex);

	/*
	 * DFS
	 */
	if (!list_empty(&cgrp->children))
		curr = list_first_entry(&cgrp->children,
					struct cgroup, sibling);
	else
		goto out;

visit_children:
	priority = atomic_read(&oom_css_from_cgroup(curr)->priority);

	if (priority > effective_priority) {
		atomic_set(&oom_css_from_cgroup(curr)->
					effective_priority, priority);
		effective_priority = priority;
	} else 
		atomic_set(&oom_css_from_cgroup(curr)->
				effective_priority,effective_priority);

	if (!list_empty(&curr->children)) {
		curr = list_first_entry(&curr->children,
						struct cgroup, sibling);
		goto visit_children;
	} else {
visit_siblings:
		if (curr == 0 || cgrp == curr)
			goto out;

		if (curr->parent)
       	        	effective_priority =
			  atomic_read(&oom_css_from_cgroup(
			   curr->parent)->effective_priority);
		else
        		effective_priority = val;

		if (curr->sibling.next != &curr->parent->children) {
			curr = list_entry(curr->sibling.next,
						struct cgroup, sibling);
			goto visit_children;
		} else {
			curr = curr->parent;
			goto visit_siblings;
		}
	}
out:
				
		mutex_unlock(&oom_subsys.hierarchy_mutex);

}

static int oom_priority_write(struct cgroup *cgrp, struct cftype *cft,
                                       u64 val)
{
	u64 effective_priority;
	u64 old_priority;
	u64 parent_effective_priority = 0;

	old_priority = atomic_read(&(oom_css_from_cgroup(cgrp))->priority);
	atomic_set(&(oom_css_from_cgroup(cgrp))->priority, val);

	effective_priority = atomic_read(
			&(oom_css_from_cgroup(cgrp))->effective_priority);

	/*
	 * propagate new effective_priority to sub cgroups
	 */
	if (val > effective_priority)
		increase_effective_priority(cgrp, val);
	else if (effective_priority == old_priority &&
						val < effective_priority) {
		struct oom_cgroup *oom_css = NULL;
		if (cgrp->parent)
			oom_css = oom_css_from_cgroup(cgrp->parent);
		else
			oom_css = oom_css_from_cgroup(cgrp);

		if (cgrp->parent)
			parent_effective_priority =
				atomic_read(&oom_css->effective_priority);
			
		if (cgrp->parent == NULL || 
				parent_effective_priority < effective_priority) {
			/*
			 * set effective_priority to max of parents effective and
			 * new priority
			 */
			if (cgrp->parent == NULL || effective_priority < val
				 	|| parent_effective_priority < val)
				effective_priority = val;
			else
				effective_priority = parent_effective_priority;

			decrease_effective_priority(cgrp, effective_priority);

		} 
	}
        return 0;
}

static u64 oom_effective_priority_read(struct cgroup *cgrp, struct cftype *cft)
{
        u64 priority = atomic_read(&(oom_css_from_cgroup(cgrp))->effective_priority);

        return priority;
}

static u64 oom_priority_read(struct cgroup *cgrp, struct cftype *cft)
{
        u64 priority = atomic_read(&(oom_css_from_cgroup(cgrp))->priority);

        return priority;
}

static int oom_cpuset_write(struct cgroup *cgrp, struct cftype *cft,
					u64 val)
{
	if (val > 1)
		return -EINVAL;
	atomic_set(&honour_cpuset_constraint, val);
	return 0;
}

static u64 oom_cpuset_read(struct cgroup *cgrp, struct cftype *cft)
{
        return atomic_read(&honour_cpuset_constraint);
}

static struct cftype oom_cgroup_files[] = {
	{
		.name = "priority",
		.read_u64 = oom_priority_read,
		.write_u64 = oom_priority_write,
	},
	{
		.name = "effective_priority",
		.read_u64 = oom_effective_priority_read,
	},
};

static struct cftype oom_cgroup_root_only_files[] = {
	{
		.name = "cpuset_constraint",
		.read_u64 = oom_cpuset_read,
		.write_u64 = oom_cpuset_write,
	},
};

static int oom_populate(struct cgroup_subsys *ss,
                                struct cgroup *cont)
{
	int ret;

	ret = cgroup_add_files(cont, ss, oom_cgroup_files,
				ARRAY_SIZE(oom_cgroup_files));
	if (!ret && cont->parent == NULL) {
		ret = cgroup_add_files(cont, ss, oom_cgroup_root_only_files,
				ARRAY_SIZE(oom_cgroup_root_only_files));
	}

	return ret;
}

struct cgroup_subsys oom_subsys = {
	.name = "oom",
	.subsys_id = oom_subsys_id,
	.create = oom_create,
	.destroy = oom_destroy,
	.populate = oom_populate,
};
