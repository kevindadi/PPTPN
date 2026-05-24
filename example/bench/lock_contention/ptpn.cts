// TPN name=PTPN

typedef int place; 

initially { 
place Bentry=1, Bready=0, B_seg_1_done=0, B_hold_1=0, B_seg_2_done=0, Bexit=0, Aentry=1, Aready=0, A_seg_1_done=0, A_hold_1=0, A_seg_2_done=0, Aexit=0, core0=1, core1=1, mutex1=1; }

 transition [priority=2099, intermediate { Bentry = Bentry - 1 , core1 = core1 - 1; }]  Bget_core [0,0]
      when (Bentry >= 1 and core1 >= 1)
      { Bentry = Bentry - 1 , Bready = Bready + 1 , core1 = core1 - 1;  }
 transition [priority=2099, intermediate { Bready = Bready - 1; }]  B_exec_1 [1,1]
      when (Bready >= 1)
      { Bready = Bready - 1 , B_seg_1_done = B_seg_1_done + 1;  }
 transition [priority=2099, intermediate { B_seg_1_done = B_seg_1_done - 1 , mutex1 = mutex1 - 1; }]  B_lock_1 [0,0]
      when (B_seg_1_done >= 1 and mutex1 >= 1)
      { B_seg_1_done = B_seg_1_done - 1 , B_hold_1 = B_hold_1 + 1 , mutex1 = mutex1 - 1;  }
 transition [priority=2099, intermediate { B_hold_1 = B_hold_1 - 1; }]  B_exec_2 [2,4]
      when (B_hold_1 >= 1)
      { B_hold_1 = B_hold_1 - 1 , B_seg_2_done = B_seg_2_done + 1 , mutex1 = mutex1 + 1;  }
 transition [priority=2099, intermediate { B_seg_2_done = B_seg_2_done - 1; }]  B_exec_3 [1,2]
      when (B_seg_2_done >= 1)
      { B_seg_2_done = B_seg_2_done - 1 , Bexit = Bexit + 1 , core1 = core1 + 1;  }
 transition [priority=1099, intermediate { Aentry = Aentry - 1 , core0 = core0 - 1; }]  Aget_core [0,0]
      when (Aentry >= 1 and core0 >= 1)
      { Aentry = Aentry - 1 , Aready = Aready + 1 , core0 = core0 - 1;  }
 transition [priority=1099, intermediate { Aready = Aready - 1; }]  A_exec_1 [1,2]
      when (Aready >= 1)
      { Aready = Aready - 1 , A_seg_1_done = A_seg_1_done + 1;  }
 transition [priority=1099, intermediate { A_seg_1_done = A_seg_1_done - 1 , mutex1 = mutex1 - 1; }]  A_lock_1 [0,0]
      when (A_seg_1_done >= 1 and mutex1 >= 1)
      { A_seg_1_done = A_seg_1_done - 1 , A_hold_1 = A_hold_1 + 1 , mutex1 = mutex1 - 1;  }
 transition [priority=1099, intermediate { A_hold_1 = A_hold_1 - 1; }]  A_exec_2 [3,5]
      when (A_hold_1 >= 1)
      { A_hold_1 = A_hold_1 - 1 , A_seg_2_done = A_seg_2_done + 1 , mutex1 = mutex1 + 1;  }
 transition [priority=1099, intermediate { A_seg_2_done = A_seg_2_done - 1; }]  A_exec_3 [1,1]
      when (A_seg_2_done >= 1)
      { A_seg_2_done = A_seg_2_done - 1 , Aexit = Aexit + 1 , core0 = core0 + 1;  }
 transition [priority=0, intermediate { Aexit = Aexit - 1; }]  A_consume [0,0]
      when (Aexit >= 1)
      { Aexit = Aexit - 1;  }
 transition [priority=0, intermediate { Bexit = Bexit - 1; }]  B_consume [0,0]
      when (Bexit >= 1)
      { Bexit = Bexit - 1;  }

graph [passed=eq]
