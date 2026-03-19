use criterion::{black_box, criterion_group, criterion_main, Criterion};
use ptpn::{matrix_to_graph, parse_dot_file, save_to_dot, MatrixPTPN, StateClassReachabilityGraph};
use std::path::Path;

fn bench_full_pipeline(c: &mut Criterion) {
    let path = Path::new("example/common.dot");
    if !path.exists() {
        eprintln!("Skipping benchmark: example/common.dot not found");
        return;
    }
    c.bench_function("full_pipeline", |b| {
        b.iter(|| {
            let mut tdg = parse_dot_file(black_box(path), 1, 2).unwrap();
            let mut matrix = MatrixPTPN::new();
            matrix.transform_tdg_to_matrix_ptpn(&mut tdg);
            let graph = matrix_to_graph(&matrix);
            let _ = save_to_dot(&graph, "/tmp/ptpn_bench.dot");
            let mut scg = StateClassReachabilityGraph::new(matrix);
            scg.build(1000);
        });
    });
}

criterion_group!(benches, bench_full_pipeline);
criterion_main!(benches);
