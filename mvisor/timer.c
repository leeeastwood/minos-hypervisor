/*
 * timer.c refer to linux kernel timer code
 */

#include <mvisor/mvisor.h>
#include <mvisor/list.h>
#include <mvisor/timer.h>
#include <mvisor/softirq.h>
#include <mvisor/time.h>

DEFINE_PER_CPU(struct timers, timers);

static void run_timer_softirq(struct softirq_action *h)
{
	struct timer_list *timer;
	unsigned long expires = ~0, now;
	struct timers *timers = &get_cpu_var(timers);
	struct list_head *entry = &timers->active;
	struct list_head *next = timers->active.next;

	now = NOW();

	while (next != entry) {
		timer = list_entry(next, struct timer_list, entry);
		next = next->next;

		if (timer->expires <= (now + DEFAULT_TIMER_MARGIN)) {
			/*
			 * should aquire the spinlock ?
			 * TBD
			 */
			list_del(&timer->entry);
			timer->entry.next = NULL;
			timers->running_timer = timer;
			timer->function(timer->data);

			/*
			 * check whether this timer is add to list
			 * again
			 */
			if (timer->entry.next != NULL)
				expires = timer->expires;
		} else {
			if (expires > timer->expires)
				expires = timer->expires;
		}
	}

	if (expires != ((unsigned long)~0)) {
		timers->running_expires = expires;
		enable_timer(expires);
	}
}

static inline int timer_pending(const struct timer_list * timer)
{
	return timer->entry.next != NULL;
}

static int detach_timer(struct timers *timers, struct timer_list *timer)
{
	struct list_head *entry = &timer->entry;

	if (!timer_pending(timer))
		return 0;

	list_del(entry);
	entry->next = NULL;
}

static inline unsigned long slack_expires(unsigned long expires)
{
	if (expires < 128)
		expires = 128;

	return expires + NOW();
}

int mod_timer(struct timer_list *timer, unsigned long expires)
{
	unsigned long flags;
	struct timers *timers = &get_cpu_var(timers);

	if (timer_pending(timer) && (timer->expires == expires))
		return 1;

	expires = slack_expires(expires);
	timer->expires = expires;

	spin_lock_irqsave(&timers->lock, flags);

	detach_timer(timers, timer);
	list_add_tail(&timers->active, &timer->entry);

	/*
	 * reprogram the timer for next event
	 */
	if ((timers->running_expires > expires) ||
			(timers->running_expires == 0)) {
		timers->running_expires = expires;
		enable_timer(timers->running_expires);
	}

	spin_unlock_irqrestore(&timers->lock, flags);

	return 0;
}

void init_timer(struct timer_list *timer)
{
	BUG_ON(!timer);

	init_list(&timer->entry);
	timer->entry.next = NULL;
	timer->expires = 0;
	timer->function = NULL;
	timer->data = 0;
}

int del_timer(struct timer_list *timer)
{
	unsigned long flags;
	struct timers *timers = timer->timers;

	spin_lock_irqsave(&timers->lock, flags);
	detach_timer(timers, timer);
	spin_unlock_irqrestore(&timers->lock, flags);

	return 0;
}

void add_timer(struct timer_list *timer)
{
	BUG_ON(timer_pending(timer));
	mod_timer(timer, timer->expires);
}

void init_timers(void)
{
	int i;
	struct timers *timers;

	for (i = 0; i < CONFIG_NR_CPUS; i++) {
		timers = &get_per_cpu(timers, i);
		init_list(&timers->active);
		timers->running_expires = 0;
		spin_lock_init(&timers->lock);
	}

	open_softirq(TIMER_SOFTIRQ, run_timer_softirq);
}
