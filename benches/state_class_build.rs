use criterion::{black_box, criterion_group, criterion_main, Criterion};
use ptpn::{parse_dot_file, MatrixPTPN, StateClassReachabilityGraph};
use std::path::Path;

fn bench_state_class_small(c: &mut Criterion) {
    let path = Path::new("example/common.dot");
    if !path.exists() {
        eprintln!("Skipping benchmark: example/common.dot not found");
        return;
    }
    let mut tdg = parse_dot_file(path, 1, 2).unwrap();
    let mut matrix = MatrixPTPN::new();
    matrix.transform_tdg_to_matrix_ptpn(&mut tdg);

    c.bench_function("state_class_build_small", |b| {
        b.iter(|| {
            let mut scg = StateClassReachabilityGraph::new(matrix.clone());
            scg.build(black_box(100));
        });
    });
}

fn bench_state_class_medium(c: &mut Criterion) {
    let path = Path::new("example/common.dot");
    if !path.exists() {
        eprintln!("Skipping benchmark: example/common.dot not found");
        return;
    }
    let mut tdg = parse_dot_file(path, 1, 2).unwrap();
    let mut matrix = MatrixPTPN::new();
    matrix.transform_tdg_to_matrix_ptpn(&mut tdg);

    c.bench_function("state_class_build_medium", |b| {
        b.iter(|| {
            let mut scg = StateClassReachabilityGraph::new(matrix.clone());
            scg.build(black_box(1000));
        });
    });
}

criterion_group!(benches, bench_state_class_small, bench_state_class_medium);
criterion_main!(benches);
