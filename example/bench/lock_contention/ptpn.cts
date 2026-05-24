<?xml version="1.0" encoding="UTF-8"?>
<romeo-cts>
  <net>
    <place id="0" name="Bentry" initial="1" capacity="1"/>
    <place id="1" name="Bready" initial="0" capacity="1"/>
    <place id="2" name="B_seg_1_done" initial="0" capacity="1"/>
    <place id="3" name="B_hold_1" initial="0" capacity="1"/>
    <place id="4" name="B_seg_2_done" initial="0" capacity="1"/>
    <place id="5" name="Bexit" initial="0" capacity="1"/>
    <place id="6" name="Aentry" initial="1" capacity="1"/>
    <place id="7" name="Aready" initial="0" capacity="1"/>
    <place id="8" name="A_seg_1_done" initial="0" capacity="1"/>
    <place id="9" name="A_hold_1" initial="0" capacity="1"/>
    <place id="10" name="A_seg_2_done" initial="0" capacity="1"/>
    <place id="11" name="Aexit" initial="0" capacity="1"/>
    <place id="12" name="core0" initial="1" capacity="1"/>
    <place id="13" name="core1" initial="1" capacity="1"/>
    <place id="14" name="mutex1" initial="1" capacity="1"/>
    <transition id="0" name="Bget_core" priority="2099" earliest="0" latest="0" suspendable="false"/>
    <transition id="1" name="B_exec_1" priority="2099" earliest="1" latest="1" suspendable="false"/>
    <transition id="2" name="B_lock_1" priority="2099" earliest="0" latest="0" suspendable="false"/>
    <transition id="3" name="B_exec_2" priority="2099" earliest="2" latest="4" suspendable="false"/>
    <transition id="4" name="B_exec_3" priority="2099" earliest="1" latest="2" suspendable="false"/>
    <transition id="5" name="Aget_core" priority="1099" earliest="0" latest="0" suspendable="false"/>
    <transition id="6" name="A_exec_1" priority="1099" earliest="1" latest="2" suspendable="false"/>
    <transition id="7" name="A_lock_1" priority="1099" earliest="0" latest="0" suspendable="false"/>
    <transition id="8" name="A_exec_2" priority="1099" earliest="3" latest="5" suspendable="false"/>
    <transition id="9" name="A_exec_3" priority="1099" earliest="1" latest="1" suspendable="false"/>
    <transition id="10" name="A_consume" priority="0" earliest="0" latest="0" suspendable="false"/>
    <transition id="11" name="B_consume" priority="0" earliest="0" latest="0" suspendable="false"/>
    <arc source="0" target="0" weight="1"/>
    <arc source="1" target="1" weight="1"/>
    <arc source="2" target="2" weight="1"/>
    <arc source="3" target="3" weight="1"/>
    <arc source="4" target="4" weight="1"/>
    <arc source="5" target="11" weight="1"/>
    <arc source="6" target="5" weight="1"/>
    <arc source="7" target="6" weight="1"/>
    <arc source="8" target="7" weight="1"/>
    <arc source="9" target="8" weight="1"/>
    <arc source="10" target="9" weight="1"/>
    <arc source="11" target="10" weight="1"/>
    <arc source="12" target="5" weight="1"/>
    <arc source="13" target="0" weight="1"/>
    <arc source="14" target="2" weight="1"/>
    <arc source="14" target="7" weight="1"/>
    <arc source="0" target="1" weight="1"/>
    <arc source="1" target="2" weight="1"/>
    <arc source="2" target="3" weight="1"/>
    <arc source="3" target="4" weight="1"/>
    <arc source="3" target="14" weight="1"/>
    <arc source="4" target="5" weight="1"/>
    <arc source="4" target="13" weight="1"/>
    <arc source="5" target="7" weight="1"/>
    <arc source="6" target="8" weight="1"/>
    <arc source="7" target="9" weight="1"/>
    <arc source="8" target="10" weight="1"/>
    <arc source="8" target="14" weight="1"/>
    <arc source="9" target="11" weight="1"/>
    <arc source="9" target="12" weight="1"/>
  </net>
</romeo-cts>
