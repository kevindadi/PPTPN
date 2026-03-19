//! 图形式 PTPN
//!
//! 使用 petgraph 构建，用于 DOT 导出和可视化

use crate::ptpn::matrix::MatrixPTPN;
use crate::ptpn::time_interval::INF;
use petgraph::graph::DiGraph;
use petgraph::visit::EdgeRef;
use std::fs;
use std::path::Path;
use tracing::info;

/// 顶点：库所或变迁
#[derive(Debug, Clone)]
pub enum Vertex {
    Place { token: i32, capacity: i32 },
    Transition {
        priority: i32,
        core: i32,
        time: (i32, i32),
        suspendable: bool,
    },
}

/// 边
#[derive(Debug, Clone)]
pub struct Edge {
    pub label: String,
    pub weight: i32,
}

/// 图形式 PTPN
pub type PtpnGraph = DiGraph<(String, Vertex), Edge>;

/// 从矩阵 PTPN 构建图
pub fn matrix_to_graph(matrix: &MatrixPTPN) -> PtpnGraph {
    let mut graph = PtpnGraph::new();

    let mut place_to_node = std::collections::HashMap::new();
    let mut trans_to_node = std::collections::HashMap::new();
    let marking = matrix.get_marking();

    for (p, place) in matrix.places.iter().enumerate() {
        let token = marking.get(p).copied().unwrap_or(0);
        let capacity = place.capacity;
        let node = graph.add_node((
            place.name.clone(),
            Vertex::Place { token, capacity },
        ));
        place_to_node.insert(p, node);
    }

    for (t, trans) in matrix.transitions.iter().enumerate() {
        let time = (
            trans.time_interval.earliest,
            if trans.time_interval.latest == INF {
                i32::MAX
            } else {
                trans.time_interval.latest
            },
        );
        let node = graph.add_node((
            trans.name.clone(),
            Vertex::Transition {
                priority: trans.priority,
                core: trans.core,
                time,
                suspendable: trans.suspendable,
            },
        ));
        trans_to_node.insert(t, node);
    }

    for (p, row) in matrix.pre.iter().enumerate() {
        for (t, &w) in row.iter().enumerate() {
            if w > 0 {
                if let (Some(&from), Some(&to)) =
                    (place_to_node.get(&p), trans_to_node.get(&t))
                {
                    let label = if w > 1 { w.to_string() } else { String::new() };
                    graph.add_edge(from, to, Edge { label, weight: w });
                }
            }
        }
    }

    for (t, row) in matrix.post.iter().enumerate() {
        for (p, &w) in row.iter().enumerate() {
            if w > 0 {
                if let (Some(&from), Some(&to)) =
                    (trans_to_node.get(&t), place_to_node.get(&p))
                {
                    let label = if w > 1 { w.to_string() } else { String::new() };
                    graph.add_edge(from, to, Edge { label, weight: w });
                }
            }
        }
    }

    info!(
        "[GRAPH_PTPN] 转换完成: {} 个节点, {} 条边",
        graph.node_count(),
        graph.edge_count()
    );

    graph
}

/// 保存为 DOT 格式
pub fn save_to_dot(graph: &PtpnGraph, path: impl AsRef<Path>) -> std::io::Result<()> {
    let mut dot = String::from("digraph G {\n");

    for (idx, (name, vertex)) in graph.node_weights().enumerate() {
        let (shape, label) = match vertex {
            Vertex::Place { token, capacity } => {
                ("circle", format!("{} [t={},c={}]", name, token, capacity))
            }
            Vertex::Transition {
                priority,
                core,
                time,
                suspendable,
            } => (
                "box",
                format!(
                    "{} [p={},c={},t=[{},{}],susp={}]",
                    name, priority, core, time.0, time.1, suspendable
                ),
            ),
        };
        dot.push_str(&format!("  n{} [label=\"{}\", shape={}];\n", idx, label, shape));
    }

    for edge in graph.edge_references() {
        let from = edge.source().index();
        let to = edge.target().index();
        let w = edge.weight();
        let label = if w.label.is_empty() {
            String::new()
        } else {
            format!(" [label=\"{}\"]", w.label)
        };
        dot.push_str(&format!("  n{} -> n{}{};\n", from, to, label));
    }

    dot.push_str("}\n");

    fs::write(path, dot)
}
