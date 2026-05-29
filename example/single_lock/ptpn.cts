// TPN name=PTPN

typedef int place; 

initially { 
place TaskCentry=0, TaskCready=0, TaskC_seg_1_done=0, TaskC_hold_1=0, TaskC_seg_2_done=0, TaskCexit=0, TaskBentry=0, TaskBready=0, TaskB_seg_1_done=0, TaskB_hold_1=0, TaskB_seg_2_done=0, TaskBexit=0, TaskAentry=0, TaskAready=0, TaskA_seg_1_done=0, TaskA_hold_1=0, TaskA_seg_2_done=0, TaskAexit=0, TaskA_period=1, TaskC_period=1, TaskA_suspended_TaskC_0=0, TaskA_lock_suspended_TaskC_1=0, core0=4, core1=4, mutex1=1, spin1=1; }

 transition [priority=9999, intermediate { TaskCentry = TaskCentry - 1 , core0 = core0 - 1; }]  TaskCget_core [0,0]
      when (TaskCentry >= 1 and core0 >= 1)
      { TaskCentry = TaskCentry - 1 , TaskCready = TaskCready + 1 , core0 = core0 - 1;  }
 transition [priority=9999, intermediate { TaskCready = TaskCready - 1; }]  TaskC_exec_1 [0,20]
      when (TaskCready >= 1)
      { TaskCready = TaskCready - 1 , TaskC_seg_1_done = TaskC_seg_1_done + 1;  }
 transition [priority=9999, intermediate { TaskC_seg_1_done = TaskC_seg_1_done - 1 , mutex1 = mutex1 - 1; }]  TaskC_lock_1 [0,0]
      when (TaskC_seg_1_done >= 1 and mutex1 >= 1)
      { TaskC_seg_1_done = TaskC_seg_1_done - 1 , TaskC_hold_1 = TaskC_hold_1 + 1 , mutex1 = mutex1 - 1;  }
 transition [priority=9999, intermediate { TaskC_hold_1 = TaskC_hold_1 - 1; }]  TaskC_exec_2 [20,40]
      when (TaskC_hold_1 >= 1)
      { TaskC_hold_1 = TaskC_hold_1 - 1 , TaskC_seg_2_done = TaskC_seg_2_done + 1 , mutex1 = mutex1 + 1;  }
 transition [priority=9999, intermediate { TaskC_seg_2_done = TaskC_seg_2_done - 1; }]  TaskC_exec_3 [40,60]
      when (TaskC_seg_2_done >= 1)
      { TaskC_seg_2_done = TaskC_seg_2_done - 1 , TaskCexit = TaskCexit + 1 , core0 = core0 + 1;  }
 transition [priority=9899, intermediate { TaskBentry = TaskBentry - 1 , core1 = core1 - 1; }]  TaskBget_core [0,0]
      when (TaskBentry >= 1 and core1 >= 1)
      { TaskBentry = TaskBentry - 1 , TaskBready = TaskBready + 1 , core1 = core1 - 1;  }
 transition [priority=9899, intermediate { TaskBready = TaskBready - 1; }]  TaskB_exec_1 [0,5]
      when (TaskBready >= 1)
      { TaskBready = TaskBready - 1 , TaskB_seg_1_done = TaskB_seg_1_done + 1;  }
 transition [priority=9899, intermediate { TaskB_seg_1_done = TaskB_seg_1_done - 1 , spin1 = spin1 - 1; }]  TaskB_lock_1 [0,0]
      when (TaskB_seg_1_done >= 1 and spin1 >= 1)
      { TaskB_seg_1_done = TaskB_seg_1_done - 1 , TaskB_hold_1 = TaskB_hold_1 + 1 , spin1 = spin1 - 1;  }
 transition [priority=9899, intermediate { TaskB_hold_1 = TaskB_hold_1 - 1; }]  TaskB_exec_2 [5,10]
      when (TaskB_hold_1 >= 1)
      { TaskB_hold_1 = TaskB_hold_1 - 1 , TaskB_seg_2_done = TaskB_seg_2_done + 1 , spin1 = spin1 + 1;  }
 transition [priority=9899, intermediate { TaskB_seg_2_done = TaskB_seg_2_done - 1; }]  TaskB_exec_3 [10,15]
      when (TaskB_seg_2_done >= 1)
      { TaskB_seg_2_done = TaskB_seg_2_done - 1 , TaskBexit = TaskBexit + 1 , core1 = core1 + 1;  }
 transition [priority=9799, intermediate { TaskAentry = TaskAentry - 1 , core0 = core0 - 1; }]  TaskAget_core [0,0]
      when (TaskAentry >= 1 and core0 >= 1)
      { TaskAentry = TaskAentry - 1 , TaskAready = TaskAready + 1 , core0 = core0 - 1;  }
 transition [priority=9799, intermediate { TaskAready = TaskAready - 1; }]  TaskA_exec_1 [0,10]
      when (TaskAready >= 1)
      { TaskAready = TaskAready - 1 , TaskA_seg_1_done = TaskA_seg_1_done + 1;  }
 transition [priority=9799, intermediate { TaskA_seg_1_done = TaskA_seg_1_done - 1 , mutex1 = mutex1 - 1; }]  TaskA_lock_1 [0,0]
      when (TaskA_seg_1_done >= 1 and mutex1 >= 1)
      { TaskA_seg_1_done = TaskA_seg_1_done - 1 , TaskA_hold_1 = TaskA_hold_1 + 1 , mutex1 = mutex1 - 1;  }
 transition [priority=9799, intermediate { TaskA_hold_1 = TaskA_hold_1 - 1; }]  TaskA_exec_2 [10,30]
      when (TaskA_hold_1 >= 1)
      { TaskA_hold_1 = TaskA_hold_1 - 1 , TaskA_seg_2_done = TaskA_seg_2_done + 1 , mutex1 = mutex1 + 1;  }
 transition [priority=9799, intermediate { TaskA_seg_2_done = TaskA_seg_2_done - 1; }]  TaskA_exec_3 [30,50]
      when (TaskA_seg_2_done >= 1)
      { TaskA_seg_2_done = TaskA_seg_2_done - 1 , TaskAexit = TaskAexit + 1 , core0 = core0 + 1;  }
 transition [priority=0, intermediate { TaskAexit = TaskAexit - 1; }]  TaskA_to_TaskB [0,0]
      when (TaskAexit >= 1)
      { TaskBentry = TaskBentry + 1 , TaskAexit = TaskAexit - 1;  }
 transition [priority=0, intermediate { TaskBexit = TaskBexit - 1; }]  TaskB_to_TaskC [0,0]
      when (TaskBexit >= 1)
      { TaskCentry = TaskCentry + 1 , TaskBexit = TaskBexit - 1;  }
 transition [priority=0, intermediate { TaskA_period = TaskA_period - 1; }]  TaskA_fire [100,100]
      when (TaskA_period >= 1)
      { TaskAentry = TaskAentry + 1 , TaskA_period = TaskA_period - 1 + 1;  }
 transition [priority=0, intermediate { TaskC_period = TaskC_period - 1; }]  TaskC_fire [200,200]
      when (TaskC_period >= 1)
      { TaskCentry = TaskCentry + 1 , TaskC_period = TaskC_period - 1 + 1;  }
 transition [priority=0, intermediate { TaskCexit = TaskCexit - 1; }]  TaskC_consume [0,0]
      when (TaskCexit >= 1)
      { TaskCexit = TaskCexit - 1;  }
 transition [priority=9998, intermediate { TaskCentry = TaskCentry - 1 , TaskAready = TaskAready - 1; }]  TaskC_resume_preempt_TaskA_0 [0,0]
      when (TaskCentry >= 1 and TaskAready >= 1)
      { TaskCentry = TaskCentry - 1 , TaskCready = TaskCready + 1 , TaskAready = TaskAready - 1 , TaskA_suspended_TaskC_0 = TaskA_suspended_TaskC_0 + 1;  }
 transition [priority=0, intermediate { TaskA_suspended_TaskC_0 = TaskA_suspended_TaskC_0 - 1; }]  TaskA_resume_TaskC_0 [0,0]
      when (TaskA_suspended_TaskC_0 >= 1)
      { TaskC_seg_1_done = TaskC_seg_1_done + 1 , TaskAready = TaskAready + 1 , TaskA_suspended_TaskC_0 = TaskA_suspended_TaskC_0 - 1;  }
 transition [priority=9998, intermediate { TaskCentry = TaskCentry - 1 , TaskA_hold_1 = TaskA_hold_1 - 1; }]  TaskC_resume_lock_preempt_TaskA_1 [0,0]
      when (TaskCentry >= 1 and TaskA_hold_1 >= 1)
      { TaskCentry = TaskCentry - 1 , TaskCready = TaskCready + 1 , TaskA_hold_1 = TaskA_hold_1 - 1 , TaskA_lock_suspended_TaskC_1 = TaskA_lock_suspended_TaskC_1 + 1;  }
 transition [priority=0, intermediate { TaskA_lock_suspended_TaskC_1 = TaskA_lock_suspended_TaskC_1 - 1; }]  TaskA_lock_resume_TaskC_1 [0,0]
      when (TaskA_lock_suspended_TaskC_1 >= 1)
      { TaskA_hold_1 = TaskA_hold_1 + 1 , TaskA_lock_suspended_TaskC_1 = TaskA_lock_suspended_TaskC_1 - 1;  }

graph [passed=eq]
