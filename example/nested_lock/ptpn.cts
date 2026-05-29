// TPN name=PTPN

typedef int place; 

initially { 
place TaskBentry=0, TaskBready=0, TaskB_seg_1_done=0, TaskB_hold_1=0, TaskB_seg_2_done=0, TaskB_hold_2=0, TaskB_seg_3_done=0, TaskB_seg_4_done=0, TaskBexit=0, TaskAentry=0, TaskAready=0, TaskA_seg_1_done=0, TaskA_hold_1=0, TaskA_seg_2_done=0, TaskA_hold_2=0, TaskA_seg_3_done=0, TaskA_seg_4_done=0, TaskAexit=0, TaskA_suspended_TaskB_0=0, TaskA_lock_suspended_TaskB_1=0, TaskA_lock_suspended_TaskB_2=0, TaskA_lock_suspended_TaskB_3=0, TaskA_lock_suspended_TaskB_4=0, TaskA_lock_suspended_TaskB_5=0, core0=4, core1=4, mutex1=1, mutex2=1, spin1=1; }

 transition [priority=9899, intermediate { TaskBentry = TaskBentry - 1 , core0 = core0 - 1; }]  TaskBget_core [0,0]
      when (TaskBentry >= 1 and core0 >= 1)
      { TaskBentry = TaskBentry - 1 , TaskBready = TaskBready + 1 , core0 = core0 - 1;  }
 transition [priority=9899, intermediate { TaskBready = TaskBready - 1; }]  TaskB_exec_1 [0,5]
      when (TaskBready >= 1)
      { TaskBready = TaskBready - 1 , TaskB_seg_1_done = TaskB_seg_1_done + 1;  }
 transition [priority=9899, intermediate { TaskB_seg_1_done = TaskB_seg_1_done - 1 , spin1 = spin1 - 1; }]  TaskB_lock_1 [0,0]
      when (TaskB_seg_1_done >= 1 and spin1 >= 1)
      { TaskB_seg_1_done = TaskB_seg_1_done - 1 , TaskB_hold_1 = TaskB_hold_1 + 1 , spin1 = spin1 - 1;  }
 transition [priority=9899, intermediate { TaskB_hold_1 = TaskB_hold_1 - 1; }]  TaskB_exec_2 [5,10]
      when (TaskB_hold_1 >= 1)
      { TaskB_hold_1 = TaskB_hold_1 - 1 , TaskB_seg_2_done = TaskB_seg_2_done + 1;  }
 transition [priority=9899, intermediate { TaskB_seg_2_done = TaskB_seg_2_done - 1 , mutex2 = mutex2 - 1; }]  TaskB_lock_2 [0,0]
      when (TaskB_seg_2_done >= 1 and mutex2 >= 1)
      { TaskB_seg_2_done = TaskB_seg_2_done - 1 , TaskB_hold_2 = TaskB_hold_2 + 1 , mutex2 = mutex2 - 1;  }
 transition [priority=9899, intermediate { TaskB_hold_2 = TaskB_hold_2 - 1; }]  TaskB_exec_3 [10,15]
      when (TaskB_hold_2 >= 1)
      { TaskB_hold_2 = TaskB_hold_2 - 1 , TaskB_seg_3_done = TaskB_seg_3_done + 1 , mutex2 = mutex2 + 1;  }
 transition [priority=9899, intermediate { TaskB_seg_3_done = TaskB_seg_3_done - 1; }]  TaskB_exec_4 [15,20]
      when (TaskB_seg_3_done >= 1)
      { TaskB_seg_3_done = TaskB_seg_3_done - 1 , TaskB_seg_4_done = TaskB_seg_4_done + 1 , spin1 = spin1 + 1;  }
 transition [priority=9899, intermediate { TaskB_seg_4_done = TaskB_seg_4_done - 1; }]  TaskB_exec_5 [20,30]
      when (TaskB_seg_4_done >= 1)
      { TaskB_seg_4_done = TaskB_seg_4_done - 1 , TaskBexit = TaskBexit + 1 , core0 = core0 + 1;  }
 transition [priority=9799, intermediate { TaskAentry = TaskAentry - 1 , core0 = core0 - 1; }]  TaskAget_core [0,0]
      when (TaskAentry >= 1 and core0 >= 1)
      { TaskAentry = TaskAentry - 1 , TaskAready = TaskAready + 1 , core0 = core0 - 1;  }
 transition [priority=9799, intermediate { TaskAready = TaskAready - 1; }]  TaskA_exec_1 [0,10]
      when (TaskAready >= 1)
      { TaskAready = TaskAready - 1 , TaskA_seg_1_done = TaskA_seg_1_done + 1;  }
 transition [priority=9799, intermediate { TaskA_seg_1_done = TaskA_seg_1_done - 1 , mutex1 = mutex1 - 1; }]  TaskA_lock_1 [0,0]
      when (TaskA_seg_1_done >= 1 and mutex1 >= 1)
      { TaskA_seg_1_done = TaskA_seg_1_done - 1 , TaskA_hold_1 = TaskA_hold_1 + 1 , mutex1 = mutex1 - 1;  }
 transition [priority=9799, intermediate { TaskA_hold_1 = TaskA_hold_1 - 1; }]  TaskA_exec_2 [10,20]
      when (TaskA_hold_1 >= 1)
      { TaskA_hold_1 = TaskA_hold_1 - 1 , TaskA_seg_2_done = TaskA_seg_2_done + 1;  }
 transition [priority=9799, intermediate { TaskA_seg_2_done = TaskA_seg_2_done - 1 , spin1 = spin1 - 1; }]  TaskA_lock_2 [0,0]
      when (TaskA_seg_2_done >= 1 and spin1 >= 1)
      { TaskA_seg_2_done = TaskA_seg_2_done - 1 , TaskA_hold_2 = TaskA_hold_2 + 1 , spin1 = spin1 - 1;  }
 transition [priority=9799, intermediate { TaskA_hold_2 = TaskA_hold_2 - 1; }]  TaskA_exec_3 [20,30]
      when (TaskA_hold_2 >= 1)
      { TaskA_hold_2 = TaskA_hold_2 - 1 , TaskA_seg_3_done = TaskA_seg_3_done + 1 , spin1 = spin1 + 1;  }
 transition [priority=9799, intermediate { TaskA_seg_3_done = TaskA_seg_3_done - 1; }]  TaskA_exec_4 [30,40]
      when (TaskA_seg_3_done >= 1)
      { TaskA_seg_3_done = TaskA_seg_3_done - 1 , TaskA_seg_4_done = TaskA_seg_4_done + 1 , mutex1 = mutex1 + 1;  }
 transition [priority=9799, intermediate { TaskA_seg_4_done = TaskA_seg_4_done - 1; }]  TaskA_exec_5 [40,100]
      when (TaskA_seg_4_done >= 1)
      { TaskA_seg_4_done = TaskA_seg_4_done - 1 , TaskAexit = TaskAexit + 1 , core0 = core0 + 1;  }
 transition [priority=0, intermediate { TaskAexit = TaskAexit - 1; }]  TaskA_to_TaskB [0,0]
      when (TaskAexit >= 1)
      { TaskBentry = TaskBentry + 1 , TaskAexit = TaskAexit - 1;  }
 transition [priority=0, intermediate { TaskBexit = TaskBexit - 1; }]  TaskB_consume [0,0]
      when (TaskBexit >= 1)
      { TaskBexit = TaskBexit - 1;  }
 transition [priority=9898, intermediate { TaskBentry = TaskBentry - 1 , TaskAready = TaskAready - 1; }]  TaskB_resume_preempt_TaskA_0 [0,0]
      when (TaskBentry >= 1 and TaskAready >= 1)
      { TaskBentry = TaskBentry - 1 , TaskBready = TaskBready + 1 , TaskAready = TaskAready - 1 , TaskA_suspended_TaskB_0 = TaskA_suspended_TaskB_0 + 1;  }
 transition [priority=0, intermediate { TaskB_seg_1_done = TaskB_seg_1_done - 1 , TaskA_suspended_TaskB_0 = TaskA_suspended_TaskB_0 - 1; }]  TaskA_resume_TaskB_0 [0,0]
      when (TaskB_seg_1_done >= 1 and TaskA_suspended_TaskB_0 >= 1)
      { TaskBready = TaskBready + 1 , TaskB_seg_1_done = TaskB_seg_1_done - 1 + 1 , TaskAready = TaskAready + 1 , TaskA_suspended_TaskB_0 = TaskA_suspended_TaskB_0 - 1;  }
 transition [priority=9898, intermediate { TaskBentry = TaskBentry - 1 , TaskA_seg_1_done = TaskA_seg_1_done - 1; }]  TaskB_resume_lock_preempt_TaskA_1 [0,0]
      when (TaskBentry >= 1 and TaskA_seg_1_done >= 1)
      { TaskBentry = TaskBentry - 1 , TaskBready = TaskBready + 1 , TaskA_seg_1_done = TaskA_seg_1_done - 1 , TaskA_lock_suspended_TaskB_1 = TaskA_lock_suspended_TaskB_1 + 1;  }
 transition [priority=0, intermediate { TaskB_seg_1_done = TaskB_seg_1_done - 1 , TaskA_lock_suspended_TaskB_1 = TaskA_lock_suspended_TaskB_1 - 1; }]  TaskA_lock_resume_TaskB_1 [0,0]
      when (TaskB_seg_1_done >= 1 and TaskA_lock_suspended_TaskB_1 >= 1)
      { TaskBready = TaskBready + 1 , TaskB_seg_1_done = TaskB_seg_1_done - 1 + 1 , TaskA_seg_1_done = TaskA_seg_1_done + 1 , TaskA_lock_suspended_TaskB_1 = TaskA_lock_suspended_TaskB_1 - 1;  }
 transition [priority=9898, intermediate { TaskBentry = TaskBentry - 1 , TaskA_hold_1 = TaskA_hold_1 - 1; }]  TaskB_resume_lock_preempt_TaskA_2 [0,0]
      when (TaskBentry >= 1 and TaskA_hold_1 >= 1)
      { TaskBentry = TaskBentry - 1 , TaskBready = TaskBready + 1 , TaskA_hold_1 = TaskA_hold_1 - 1 , TaskA_lock_suspended_TaskB_2 = TaskA_lock_suspended_TaskB_2 + 1;  }
 transition [priority=0, intermediate { TaskB_seg_1_done = TaskB_seg_1_done - 1 , TaskA_lock_suspended_TaskB_2 = TaskA_lock_suspended_TaskB_2 - 1; }]  TaskA_lock_resume_TaskB_2 [0,0]
      when (TaskB_seg_1_done >= 1 and TaskA_lock_suspended_TaskB_2 >= 1)
      { TaskBready = TaskBready + 1 , TaskB_seg_1_done = TaskB_seg_1_done - 1 + 1 , TaskA_hold_1 = TaskA_hold_1 + 1 , TaskA_lock_suspended_TaskB_2 = TaskA_lock_suspended_TaskB_2 - 1;  }
 transition [priority=9898, intermediate { TaskBentry = TaskBentry - 1 , TaskA_seg_2_done = TaskA_seg_2_done - 1; }]  TaskB_resume_lock_preempt_TaskA_3 [0,0]
      when (TaskBentry >= 1 and TaskA_seg_2_done >= 1)
      { TaskBentry = TaskBentry - 1 , TaskBready = TaskBready + 1 , TaskA_seg_2_done = TaskA_seg_2_done - 1 , TaskA_lock_suspended_TaskB_3 = TaskA_lock_suspended_TaskB_3 + 1;  }
 transition [priority=0, intermediate { TaskB_seg_1_done = TaskB_seg_1_done - 1 , TaskA_lock_suspended_TaskB_3 = TaskA_lock_suspended_TaskB_3 - 1; }]  TaskA_lock_resume_TaskB_3 [0,0]
      when (TaskB_seg_1_done >= 1 and TaskA_lock_suspended_TaskB_3 >= 1)
      { TaskBready = TaskBready + 1 , TaskB_seg_1_done = TaskB_seg_1_done - 1 + 1 , TaskA_seg_2_done = TaskA_seg_2_done + 1 , TaskA_lock_suspended_TaskB_3 = TaskA_lock_suspended_TaskB_3 - 1;  }
 transition [priority=9898, intermediate { TaskBentry = TaskBentry - 1 , TaskA_seg_3_done = TaskA_seg_3_done - 1; }]  TaskB_resume_lock_preempt_TaskA_4 [0,0]
      when (TaskBentry >= 1 and TaskA_seg_3_done >= 1)
      { TaskBentry = TaskBentry - 1 , TaskBready = TaskBready + 1 , TaskA_seg_3_done = TaskA_seg_3_done - 1 , TaskA_lock_suspended_TaskB_4 = TaskA_lock_suspended_TaskB_4 + 1;  }
 transition [priority=0, intermediate { TaskB_seg_1_done = TaskB_seg_1_done - 1 , TaskA_lock_suspended_TaskB_4 = TaskA_lock_suspended_TaskB_4 - 1; }]  TaskA_lock_resume_TaskB_4 [0,0]
      when (TaskB_seg_1_done >= 1 and TaskA_lock_suspended_TaskB_4 >= 1)
      { TaskBready = TaskBready + 1 , TaskB_seg_1_done = TaskB_seg_1_done - 1 + 1 , TaskA_seg_3_done = TaskA_seg_3_done + 1 , TaskA_lock_suspended_TaskB_4 = TaskA_lock_suspended_TaskB_4 - 1;  }
 transition [priority=9898, intermediate { TaskBentry = TaskBentry - 1 , TaskA_seg_4_done = TaskA_seg_4_done - 1; }]  TaskB_resume_lock_preempt_TaskA_5 [0,0]
      when (TaskBentry >= 1 and TaskA_seg_4_done >= 1)
      { TaskBentry = TaskBentry - 1 , TaskBready = TaskBready + 1 , TaskA_seg_4_done = TaskA_seg_4_done - 1 , TaskA_lock_suspended_TaskB_5 = TaskA_lock_suspended_TaskB_5 + 1;  }
 transition [priority=0, intermediate { TaskB_seg_1_done = TaskB_seg_1_done - 1 , TaskA_lock_suspended_TaskB_5 = TaskA_lock_suspended_TaskB_5 - 1; }]  TaskA_lock_resume_TaskB_5 [0,0]
      when (TaskB_seg_1_done >= 1 and TaskA_lock_suspended_TaskB_5 >= 1)
      { TaskBready = TaskBready + 1 , TaskB_seg_1_done = TaskB_seg_1_done - 1 + 1 , TaskA_seg_4_done = TaskA_seg_4_done + 1 , TaskA_lock_suspended_TaskB_5 = TaskA_lock_suspended_TaskB_5 - 1;  }

graph [passed=eq]
