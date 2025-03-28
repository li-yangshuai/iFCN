module top (x0, x1, x2, x3, x4, z0);
  input  x0, x1, x2, x3, x4;
  output z0;
  wire n12, n13, n14, n15, n16, n17, n18, n19, n20, n22, n23, n24, n28, n29;

  assign n15 = x1 & x2;
  assign n16 = ~x1 & ~x2;
  assign n19 = ~x1 & x2;
  assign n20 = ~x2 & x1;
  assign n23 = ~x3 & x4;
  assign n24 = ~x4 & x3;
  assign n18 = ~n19 & ~n20;
  assign n14 = ~n15 & ~n16;
  assign n13 = ~n14 & x0;
  assign n17 = ~x0 & ~n18;
  assign n22 = ~n23 & ~n24;
  assign n12 = ~n13 & ~n17;
  assign n28 = ~n12 & n22;
  assign n29 = ~n22 & n12;
  assign z0 = n28 | n29;

endmodule
