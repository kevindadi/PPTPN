<?xml version="1.0" encoding="UTF-8"?>
<romeo-cts>
  <net>
    <place id="0" name="TaskCentry" initial="0" capacity="1"/>
    <place id="1" name="TaskCready" initial="0" capacity="1"/>
    <place id="2" name="TaskC_seg_1_done" initial="0" capacity="1"/>
    <place id="3" name="TaskC_hold_1" initial="0" capacity="1"/>
    <place id="4" name="TaskC_seg_2_done" initial="0" capacity="1"/>
    <place id="5" name="TaskC_hold_2" initial="0" capacity="1"/>
    <place id="6" name="TaskC_seg_3_done" initial="0" capacity="1"/>
    <place id="7" name="TaskC_seg_4_done" initial="0" capacity="1"/>
    <place id="8" name="TaskCexit" initial="0" capacity="1"/>
    <place id="9" name="TaskBentry" initial="0" capacity="1"/>
    <place id="10" name="TaskBready" initial="0" capacity="1"/>
    <place id="11" name="TaskB_seg_1_done" initial="0" capacity="1"/>
    <place id="12" name="TaskB_hold_1" initial="0" capacity="1"/>
    <place id="13" name="TaskB_seg_2_done" initial="0" capacity="1"/>
    <place id="14" name="TaskB_hold_2" initial="0" capacity="1"/>
    <place id="15" name="TaskB_seg_3_done" initial="0" capacity="1"/>
    <place id="16" name="TaskB_seg_4_done" initial="0" capacity="1"/>
    <place id="17" name="TaskBexit" initial="0" capacity="1"/>
    <place id="18" name="TaskAentry" initial="0" capacity="1"/>
    <place id="19" name="TaskAready" initial="0" capacity="1"/>
    <place id="20" name="TaskA_seg_1_done" initial="0" capacity="1"/>
    <place id="21" name="TaskA_hold_1" initial="0" capacity="1"/>
    <place id="22" name="TaskA_seg_2_done" initial="0" capacity="1"/>
    <place id="23" name="TaskA_hold_2" initial="0" capacity="1"/>
    <place id="24" name="TaskA_seg_3_done" initial="0" capacity="1"/>
    <place id="25" name="TaskA_seg_4_done" initial="0" capacity="1"/>
    <place id="26" name="TaskAexit" initial="0" capacity="1"/>
    <place id="27" name="TaskA_period" initial="1" capacity="1"/>
    <place id="28" name="TaskC_period" initial="1" capacity="1"/>
    <place id="29" name="TaskA_suspended_TaskC_0" initial="0" capacity="1"/>
    <place id="30" name="TaskA_lock_suspended_TaskC_1" initial="0" capacity="1"/>
    <place id="31" name="core0" initial="4" capacity="4"/>
    <place id="32" name="core1" initial="4" capacity="4"/>
    <place id="33" name="mutex1" initial="1" capacity="1"/>
    <place id="34" name="mutex2" initial="1" capacity="1"/>
    <place id="35" name="spin1" initial="1" capacity="1"/>
    <transition id="0" name="TaskCget_core" priority="9999" earliest="0" latest="0" suspendable="false"/>
    <transition id="1" name="TaskC_exec_1" priority="9999" earliest="0" latest="10" suspendable="false"/>
    <transition id="2" name="TaskC_lock_1" priority="9999" earliest="0" latest="0" suspendable="false"/>
    <transition id="3" name="TaskC_exec_2" priority="9999" earliest="10" latest="20" suspendable="false"/>
    <transition id="4" name="TaskC_lock_2" priority="9999" earliest="0" latest="0" suspendable="false"/>
    <transition id="5" name="TaskC_exec_3" priority="9999" earliest="20" latest="30" suspendable="false"/>
    <transition id="6" name="TaskC_exec_4" priority="9999" earliest="30" latest="40" suspendable="false"/>
    <transition id="7" name="TaskC_exec_5" priority="9999" earliest="40" latest="80" suspendable="false"/>
    <transition id="8" name="TaskBget_core" priority="9899" earliest="0" latest="0" suspendable="false"/>
    <transition id="9" name="TaskB_exec_1" priority="9899" earliest="0" latest="5" suspendable="false"/>
    <transition id="10" name="TaskB_lock_1" priority="9899" earliest="0" latest="0" suspendable="false"/>
    <transition id="11" name="TaskB_exec_2" priority="9899" earliest="5" latest="10" suspendable="false"/>
    <transition id="12" name="TaskB_lock_2" priority="9899" earliest="0" latest="0" suspendable="false"/>
    <transition id="13" name="TaskB_exec_3" priority="9899" earliest="10" latest="15" suspendable="true"/>
    <transition id="14" name="TaskB_exec_4" priority="9899" earliest="15" latest="20" suspendable="false"/>
    <transition id="15" name="TaskB_exec_5" priority="9899" earliest="20" latest="30" suspendable="false"/>
    <transition id="16" name="TaskAget_core" priority="9799" earliest="0" latest="0" suspendable="false"/>
    <transition id="17" name="TaskA_exec_1" priority="9799" earliest="0" latest="10" suspendable="true"/>
    <transition id="18" name="TaskA_lock_1" priority="9799" earliest="0" latest="0" suspendable="false"/>
    <transition id="19" name="TaskA_exec_2" priority="9799" earliest="10" latest="20" suspendable="false"/>
    <transition id="20" name="TaskA_lock_2" priority="9799" earliest="0" latest="0" suspendable="false"/>
    <transition id="21" name="TaskA_exec_3" priority="9799" earliest="20" latest="30" suspendable="false"/>
    <transition id="22" name="TaskA_exec_4" priority="9799" earliest="30" latest="40" suspendable="false"/>
    <transition id="23" name="TaskA_exec_5" priority="9799" earliest="40" latest="100" suspendable="false"/>
    <transition id="24" name="TaskA_to_TaskB" priority="0" earliest="0" latest="0" suspendable="false"/>
    <transition id="25" name="TaskB_to_TaskC" priority="0" earliest="0" latest="0" suspendable="false"/>
    <transition id="26" name="TaskA_fire" priority="0" earliest="100" latest="100" suspendable="false"/>
    <transition id="27" name="TaskC_fire" priority="0" earliest="200" latest="200" suspendable="false"/>
    <transition id="28" name="TaskC_consume" priority="0" earliest="0" latest="0" suspendable="false"/>
    <transition id="29" name="TaskC_resume_preempt_TaskA_0" priority="9998" earliest="0" latest="0" suspendable="false"/>
    <transition id="30" name="TaskA_resume_TaskC_0" priority="0" earliest="0" latest="0" suspendable="false"/>
    <transition id="31" name="TaskC_resume_lock_preempt_TaskA_1" priority="9998" earliest="0" latest="0" suspendable="false"/>
    <transition id="32" name="TaskA_lock_resume_TaskC_1" priority="0" earliest="0" latest="0" suspendable="false"/>
    <arc source="0" target="0" weight="1"/>
    <arc source="0" target="29" weight="1"/>
    <arc source="0" target="31" weight="1"/>
    <arc source="1" target="1" weight="1"/>
    <arc source="2" target="2" weight="1"/>
    <arc source="2" target="30" weight="1"/>
    <arc source="2" target="32" weight="1"/>
    <arc source="3" target="3" weight="1"/>
    <arc source="4" target="4" weight="1"/>
    <arc source="5" target="5" weight="1"/>
    <arc source="6" target="6" weight="1"/>
    <arc source="7" target="7" weight="1"/>
    <arc source="8" target="28" weight="1"/>
    <arc source="9" target="8" weight="1"/>
    <arc source="10" target="9" weight="1"/>
    <arc source="11" target="10" weight="1"/>
    <arc source="12" target="11" weight="1"/>
    <arc source="13" target="12" weight="1"/>
    <arc source="14" target="13" weight="1"/>
    <arc source="15" target="14" weight="1"/>
    <arc source="16" target="15" weight="1"/>
    <arc source="17" target="25" weight="1"/>
    <arc source="18" target="16" weight="1"/>
    <arc source="19" target="17" weight="1"/>
    <arc source="19" target="29" weight="1"/>
    <arc source="20" target="18" weight="1"/>
    <arc source="21" target="19" weight="1"/>
    <arc source="22" target="20" weight="1"/>
    <arc source="23" target="21" weight="1"/>
    <arc source="24" target="22" weight="1"/>
    <arc source="24" target="31" weight="1"/>
    <arc source="25" target="23" weight="1"/>
    <arc source="26" target="24" weight="1"/>
    <arc source="27" target="26" weight="1"/>
    <arc source="28" target="27" weight="1"/>
    <arc source="29" target="30" weight="1"/>
    <arc source="30" target="32" weight="1"/>
    <arc source="31" target="0" weight="1"/>
    <arc source="31" target="16" weight="1"/>
    <arc source="32" target="8" weight="1"/>
    <arc source="33" target="4" weight="1"/>
    <arc source="33" target="18" weight="1"/>
    <arc source="34" target="2" weight="1"/>
    <arc source="34" target="12" weight="1"/>
    <arc source="35" target="10" weight="1"/>
    <arc source="35" target="20" weight="1"/>
    <arc source="0" target="1" weight="1"/>
    <arc source="1" target="2" weight="1"/>
    <arc source="2" target="3" weight="1"/>
    <arc source="3" target="4" weight="1"/>
    <arc source="4" target="5" weight="1"/>
    <arc source="5" target="6" weight="1"/>
    <arc source="5" target="33" weight="1"/>
    <arc source="6" target="7" weight="1"/>
    <arc source="6" target="34" weight="1"/>
    <arc source="7" target="8" weight="1"/>
    <arc source="7" target="31" weight="1"/>
    <arc source="8" target="10" weight="1"/>
    <arc source="9" target="11" weight="1"/>
    <arc source="10" target="12" weight="1"/>
    <arc source="11" target="13" weight="1"/>
    <arc source="12" target="14" weight="1"/>
    <arc source="13" target="15" weight="1"/>
    <arc source="13" target="34" weight="1"/>
    <arc source="14" target="16" weight="1"/>
    <arc source="14" target="35" weight="1"/>
    <arc source="15" target="17" weight="1"/>
    <arc source="15" target="32" weight="1"/>
    <arc source="16" target="19" weight="1"/>
    <arc source="17" target="20" weight="1"/>
    <arc source="18" target="21" weight="1"/>
    <arc source="19" target="22" weight="1"/>
    <arc source="20" target="23" weight="1"/>
    <arc source="21" target="24" weight="1"/>
    <arc source="21" target="35" weight="1"/>
    <arc source="22" target="25" weight="1"/>
    <arc source="22" target="33" weight="1"/>
    <arc source="23" target="26" weight="1"/>
    <arc source="23" target="31" weight="1"/>
    <arc source="24" target="9" weight="1"/>
    <arc source="25" target="0" weight="1"/>
    <arc source="26" target="18" weight="1"/>
    <arc source="26" target="27" weight="1"/>
    <arc source="27" target="0" weight="1"/>
    <arc source="27" target="28" weight="1"/>
    <arc source="29" target="1" weight="1"/>
    <arc source="29" target="29" weight="1"/>
    <arc source="30" target="19" weight="1"/>
    <arc source="31" target="1" weight="1"/>
    <arc source="31" target="30" weight="1"/>
    <arc source="32" target="24" weight="1"/>
  </net>
</romeo-cts>
