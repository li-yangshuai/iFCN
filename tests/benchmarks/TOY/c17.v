module top ( 
    i1 , i2, i3, i4, i5,
    o1, o2  );
  input  i1 , i2, i3, i4, i5;
  output o1, o2;
  wire n8, n9, n10, n12;
  assign n8 = i1  & i3;
  assign n9 = i3 & i4;
  assign n10 = i2 & ~n9;
  assign o1 = n8 | n10;
  assign n12 = i5 & ~n9;
  assign o2 = n10 | n12;
endmodule


