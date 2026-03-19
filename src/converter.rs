//! TDG 到 R-PTPN PTPN 格式转换
//!
//! 将解析后的 TDG 转换为 ptpn::model::PTPN 结构

use crate::tdg::{classify_priority, NodeType, Tdg};
use ptpn::model::{Interval, PTPN, TaskPlaces};
use ptpn::rational::Q;
use std::collections::{HashMap, HashSet};
use tracing::info;

/// 将 TDG 转换为 R-PTPN 的 PTPN
pub fn tdg_to_ptpn(tdg: &mut Tdg) -> PTPN {
    classify_priority(tdg);

    let mut ptpn = PTPN::new();

    let num_cores = (tdg.num_cpus * tdg.cores_per_cpu) as u32;
    ptpn.p2.insert("cpu".to_string());
    ptpn.m0.insert("cpu".to_string(), num_cores);

    let mut task_id: u32 = 1;
    let mut node_to_chain: HashMap<String, (String, String, String, String, String)> =
        HashMap::new();

    for node_type in &tdg.all_task {
        let (name, _core, priority, exec_lo, exec_hi, is_periodic, period_lo, period_hi) =
            match node_type {
                NodeType::Periodic(p) => {
                    let exec = p.time.first().copied().unwrap_or((0, 0));
                    (
                        p.name.clone(),
                        p.core,
                        p.priority as u32,
                        exec.0,
                        exec.1,
                        true,
                        p.period_time.0,
                        p.period_time.1,
                    )
                }
                NodeType::Aperiodic(a) => {
                    let exec = a.time.first().copied().unwrap_or((0, 0));
                    (
                        a.name.clone(),
                        a.core,
                        a.priority as u32,
                        exec.0,
                        exec.1,
                        false,
                        0,
                        0,
                    )
                }
                _ => continue,
            };

        let entry = format!("{}_entry", name);
        let get_core = format!("{}_get_core", name);
        let ready = format!("{}_ready", name);
        let exec = format!("{}_exec", name);
        let exit = format!("{}_exit", name);

        ptpn.p1.insert(entry.clone());
        ptpn.p1.insert(ready.clone());
        ptpn.p1.insert(exit.clone());

        ptpn.t1.insert(get_core.clone());
        ptpn.t1.insert(exec.clone());

        let add_arc = |f: &mut HashMap<(String, String), u32>, from: &str, to: &str, w: u32| {
            f.insert((from.to_string(), to.to_string()), w);
        };

        add_arc(&mut ptpn.f, &entry, &get_core, 1);
        add_arc(&mut ptpn.f, "cpu", &get_core, 1);
        add_arc(&mut ptpn.f, &get_core, &ready, 1);
        add_arc(&mut ptpn.f, &ready, &exec, 1);
        add_arc(&mut ptpn.f, &exec, &exit, 1);
        add_arc(&mut ptpn.f, &exec, "cpu", 1);

        ptpn.si.insert(
            get_core.clone(),
            Interval::point(Q::from(0)),
        );
        if exec_lo == exec_hi {
            ptpn.si.insert(exec.clone(), Interval::point(Q::from(exec_lo)));
        } else {
            ptpn.si.insert(
                exec.clone(),
                Interval::new(
                    Q::from(exec_lo),
                    Some(Q::from(if exec_hi >= i32::MAX { 999999 } else { exec_hi })),
                ),
            );
        }

        ptpn.tasks.insert(task_id);
        ptpn.tak.insert(get_core.clone(), task_id);
        ptpn.tak.insert(exec.clone(), task_id);

        let mut req_cpu = HashSet::new();
        req_cpu.insert("cpu".to_string());
        ptpn.req.insert(get_core.clone(), req_cpu.clone());
        ptpn.req.insert(exec.clone(), req_cpu);

        ptpn.pri.insert(get_core.clone(), priority);
        ptpn.pri.insert(exec.clone(), priority);

        ptpn.task_places.insert(
            task_id,
            TaskPlaces {
                ready: entry.clone(),
                running: ready.clone(),
                exit: exit.clone(),
            },
        );

        if is_periodic {
            let random = format!("{}_random", name);
            let fire = format!("{}_fire", name);
            ptpn.p1.insert(random.clone());
            ptpn.t1.insert(fire.clone());

            ptpn.m0.insert(random.clone(), 1);
            add_arc(&mut ptpn.f, &random, &fire, 1);
            add_arc(&mut ptpn.f, &fire, &random, 1);
            add_arc(&mut ptpn.f, &fire, &entry, 1);

            if period_lo == period_hi {
                ptpn.si.insert(fire.clone(), Interval::point(Q::from(period_lo)));
            } else {
                ptpn.si.insert(
                    fire.clone(),
                    Interval::new(
                        Q::from(period_lo),
                        Some(Q::from(if period_hi >= i32::MAX {
                            999999
                        } else {
                            period_hi
                        })),
                    ),
                );
            }
            ptpn.periodic_transitions.insert(fire);
        }

        node_to_chain.insert(
            name.clone(),
            (entry, get_core, ready, exec, exit),
        );
        task_id += 1;
    }

    let mut has_incoming: HashSet<String> = HashSet::new();
    for (_, target_name, edge_info) in &tdg.edges {
        if edge_info.style.contains("dashed") {
            continue;
        }
        has_incoming.insert(target_name.clone());
    }

    for node_type in &tdg.all_task {
        let name = match node_type {
            NodeType::Periodic(p) => p.name.clone(),
            NodeType::Aperiodic(a) => a.name.clone(),
            _ => continue,
        };
        if !has_incoming.contains(&name) {
            let entry = format!("{}_entry", name);
            if ptpn.p1.contains(&entry) {
                ptpn.m0.insert(entry.clone(), 1);
            }
        }
    }

    for (source_name, target_name, edge_info) in &tdg.edges {
        if source_name == target_name || edge_info.style.contains("dashed") {
            continue;
        }
        let Some((_, _, _, _, source_exit)) = node_to_chain.get(source_name) else {
            continue;
        };
        let Some((target_entry, _, _, _, _)) = node_to_chain.get(target_name) else {
            continue;
        };

        let trans_name = format!("{}_to_{}", source_name, target_name);
        ptpn.t1.insert(trans_name.clone());
        ptpn.si.insert(trans_name.clone(), Interval::point(Q::from(0)));

        let add_arc = |f: &mut HashMap<(String, String), u32>, from: &str, to: &str, w: u32| {
            f.insert((from.to_string(), to.to_string()), w);
        };
        add_arc(&mut ptpn.f, source_exit, &trans_name, 1);
        add_arc(&mut ptpn.f, &trans_name, target_entry, 1);
    }

    info!(
        "[CONVERTER] TDG -> PTPN: {} places, {} transitions",
        ptpn.p1.len() + ptpn.p2.len(),
        ptpn.t1.len() + ptpn.t2.len()
    );

    ptpn
}
