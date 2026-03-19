use criterion::{black_box, criterion_group, criterion_main, Criterion};
use priority::{parse_dot_file, tdg_to_ptpn};
use ptpn::examples::three_task;
use ptpn::scg;
use std::path::Path;

fn bench_full_pipeline_example(c: &mut Criterion) {
    c.bench_function("full_pipeline_three_task", |b| {
        b.iter(|| {
            let ptpn = three_task::build_three_task_ptpn();
            let scg = scg::build_scg(&ptpn);
            black_box(&scg);
        });
    });
}

fn bench_full_pipeline_tdg(c: &mut Criterion) {
    let path = Path::new("example/common.dot");
    if !path.exists() {
        return;
    }
    c.bench_function("full_pipeline_tdg", |b| {
        b.iter(|| {
            let mut tdg = parse_dot_file(black_box(path), 1, 2).unwrap();
            let ptpn = tdg_to_ptpn(&mut tdg);
            let scg = scg::build_scg(&ptpn);
            black_box(&scg);
        });
    });
}

criterion_group!(benches, bench_full_pipeline_example, bench_full_pipeline_tdg);
criterion_main!(benches);
