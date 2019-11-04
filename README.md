# pintos Project

<br />

## Threads - busy waiting
### Key to Problem Solving
1. PintOS uses preemitive kernel threads, especially, Round-Robin method. <br />
2. That means, scheduling happens at regular intervals. <br />
3. Useful data structure is already defined at .../src/lib/kernel . <br />
4. At this point, I used a list data structure. <br />
5. PintOS will check the sleeping threads regularly as execute schedule() method. <br />

### Solution
First, I made *sleep_list()*. Then, insert kernel threads which is called by *timer_sleep()* into *sleep_list()*. <br />
These sleep threads will be blocked by *thread_blocked()* method. <br />
<br />
Also, struct thread needs new arguments, which stores data of time. <br />
I declared two arguments, *wait_cnt* and *wait_start*. <br />
*wait_cnt* is an integer value that stores how long to wait. <br />
*wait_start* is an integer value that stores time when *timer_sleep()* ft is called. <br />
<br />

```
void
thread_sleep (int64_t ticks, int64_t start)
{
	struct thread *cur = thread_current ();
	enum intr_level old_level;

	/* My new codes. When if timer_sleep ft is called, then
  BLOCK current thread and add to sleep_list.  */
	ASSERT (!intr_context ());

  old_level = intr_disable ();

	if (cur != idle_thread)
	{
		cur->wait_cnt = ticks;
		cur->wait_start = start;
    list_push_back (&sleep_list, &cur->elem);
		thread_block ();
	}
  intr_set_level (old_level);
	/* My codes end. */
	
	return;
}
```

*thread_sleep()* ft make current thread sleep. Intrrupt must have to be disabled. Before adding a thread to the *sleep_list*, it records current time and wait time information. This will be used when the thread wake-up.
<br />
<br />

```
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

	schedule_sleep();					/* Added code */

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Schedule codes for sleep list. */
void
schedule_sleep (void)
{
	struct list_elem *l = list_begin (&sleep_list);
	
	while (l != list_end (&sleep_list)) 
	{
		struct thread *t = list_entry (l, struct thread, elem);
		if ( (t->wait_cnt) < timer_elapsed(t->wait_start) )
		{
			l = list_remove (l);
			thread_unblock (t);
			t->wait_cnt = 0;
			t->wait_start = 0;
		}
		else
			l = list_next (l);
	}

	return;
}
```
You can see the *schedule_sleep()* ft inside the schedule() ft. That means, PintOS will check the *sleep_list* regularly. *schedule_sleep()* ft checks the *sleep_list* and threads which needs unblocking. *timer_elapsed()* ft is used similarly compared to original codes.
<br />
<br />

```
/* Sleeps for approximately TICKS timer ticks.  Interrupts must
   be turned on. */
void
timer_sleep (int64_t ticks) 
{
  int64_t start = timer_ticks ();

	/* Original codes, which needs interrupt. */
  /*
	ASSERT (intr_get_level () == INTR_ON);
  
	while (timer_elapsed (start) < ticks) 
    thread_yield ();
	*/
	
	thread_sleep (ticks, start);

	return;

}
```

<br />
The original method is depreciated. Instead, it will invokes the *thread_sleep()* ft.
<br />



