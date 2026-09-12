# iFCN QCA simulation paper

The manuscript uses the two-column `sigconf` layout from `acmart`, with ACM
publication metadata disabled because no target venue or author block has
been selected. It compiles with the TeX installation's `acmart` package. If a
venue-supplied `acmart.cls` and `ACM-Reference-Format.bst` are placed in the
parent directory, the local `.latexmkrc` gives those files precedence.

Build from this directory with:

```bash
cd acmart-primary/ifcn-qca-paper
latexmk -pdf -interaction=nonstopmode -halt-on-error paper.tex
```

Pagination is content-driven: the source does not force a six-page body or a
references-only seventh page. Recheck the natural page count after adding
authors, a venue header, or final experimental results because each changes
the title block or text flow.

The paper's review-response experiments are under
`../../experiments/physical_review_response_20260717/`. The primary result is
`spatial_short30/extended_summary.json`; the ablation, dielectric sweep,
production probe, GCC/Clang comparison, and frozen legacy comparison are in
the sibling subdirectories/files. All headline values in `paper.tex` are
derived from these artifacts.
