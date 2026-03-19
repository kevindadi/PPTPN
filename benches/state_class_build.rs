use criterion::{black_box, criterion_group, criterion_main, Criterion};
use priority::{parse_dot_file, tdg_to_ptpn};
use ptpn::examples::three_task;
use ptpn::scg;
use std::path::Path;

fn bench_three_task_scg(c: &mut Criterion) {
    c.bench_function("state_class_build_three_task", |b| {
        b.iter(|| {
            let ptpn = three_task::build_three_task_ptpn();
            scg::build_scg(black_box(&ptpn));
        });
    });
}

fn bench_tdg_scg(c: &mut Criterion) {
    let path = Path::new("example/common.dot");
    if !path.exists() {
        return;
    }
    let mut tdg = parse_dot_file(path, 1, 2).unwrap();
    let ptpn = tdg_to_ptpn(&mut tdg);

    c.bench_function("state_class_build_tdg", |b| {
        b.iter(|| {
            scg::build_scg(black_box(&ptpn));
        });
    });
}

criterion_group!(benches, bench_three_task_scg, bench_tdg_scg);
criterion_main!(benches);
