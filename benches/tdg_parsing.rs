use criterion::{black_box, criterion_group, criterion_main, Criterion};
use ptpn::parse_dot_file;
use std::path::Path;

fn bench_tdg_parse(c: &mut Criterion) {
    let path = Path::new("example/common.dot");
    if !path.exists() {
        eprintln!("Skipping benchmark: example/common.dot not found");
        return;
    }
    c.bench_function("tdg_parse", |b| {
        b.iter(|| {
            parse_dot_file(black_box(path), 1, 2).unwrap();
        });
    });
}

criterion_group!(benches, bench_tdg_parse);
criterion_main!(benches);
