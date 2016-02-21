use strict;
use warnings 'all';

use Getopt::Long;

# Parse command line.
our ($input_file);
our ($output_file);
parse_cmd_line ();

# Initialize type system.
our (%type, @types);
init_all_types ();

# Parse input file.
our (%ops);
our (@funcs, @opers, @order);
parse_input ();

# Produce output.
print_header ();
if ($output_file =~ /evaluate\.h$/) {
    generate_evaluate_h ();
} elsif ($output_file =~ /evaluate\.inc$/) {
    generate_evaluate_inc ();
} elsif ($output_file =~ /operations\.h$/) {
    generate_operations_h ();
} elsif ($output_file =~ /optimize\.inc$/) {
    generate_optimize_inc ();
} elsif ($output_file =~ /parse\.inc$/) {
    generate_parse_inc ();
} else {
    die "$output_file: unknown output type\n";
}
print_trailer ();

# Command line.

# Parses the command line.
#
# Initializes $input_file, $output_file.
sub parse_cmd_line {
    GetOptions ("i|input=s" => \$input_file,
		"o|output=s" => \$output_file,
		"h|help" => sub { usage (); })
      or exit 1;

    $input_file = "operations.def" if !defined $input_file;
    die "$0: output file must be specified\n" if !defined $output_file;

    open (INPUT, "<$input_file") or die "$input_file: open: $!\n";
    open (OUTPUT, ">$output_file") or die "$output_file: create: $!\n";

    select (OUTPUT);
}

sub usage {
    print <<EOF;
$0, for generating expression parsers and evaluators from definitions
usage: generate.pl -o OUTPUT [-i INPUT] [-h]
  -i INPUT    input file containing definitions (default: operations.def)
  -o OUTPUT   output file
  -h          display this help message
EOF
    exit (0);
}

our ($token);
our ($toktype);

# Types.

# Defines all our types.
#
# Initializes %type, @types.
sub init_all_types {
    # Common user-visible types used throughout evaluation trees.
    init_type ('number', 'any', C_TYPE => 'double',
	       ATOM => 'number', MANGLE => 'n', HUMAN_NAME => 'number',
	       STACK => 'ns', MISSING_VALUE => 'SYSMIS');
    init_type ('string', 'any', C_TYPE => 'struct substring',
	       ATOM => 'string', MANGLE => 's', HUMAN_NAME => 'string',
	       STACK => 'ss', MISSING_VALUE => 'empty_string');
    init_type ('boolean', 'any', C_TYPE => 'double',
	       ATOM => 'number', MANGLE => 'n', HUMAN_NAME => 'boolean',
	       STACK => 'ns', MISSING_VALUE => 'SYSMIS');

    # Format types.
    init_type ('format', 'atom');
    init_type ('ni_format', 'leaf', C_TYPE => 'const struct fmt_spec *',
	       ATOM => 'format', MANGLE => 'f',
	       HUMAN_NAME => 'num_input_format');
    init_type ('no_format', 'leaf', C_TYPE => 'const struct fmt_spec *',
	       ATOM => 'format', MANGLE => 'f',
	       HUMAN_NAME => 'num_output_format');

    # Integer types.
    init_type ('integer', 'leaf', C_TYPE => 'int',
	       ATOM => 'integer', MANGLE => 'n', HUMAN_NAME => 'integer');
    init_type ('pos_int', 'leaf', C_TYPE => 'int',
	       ATOM => 'integer', MANGLE => 'n',
	       HUMAN_NAME => 'positive_integer_constant');

    # Variable names.
    init_type ('variable', 'atom');
    init_type ('num_var', 'leaf', C_TYPE => 'const struct variable *',
	       ATOM => 'variable', MANGLE => 'Vn',
	       HUMAN_NAME => 'num_variable');
    init_type ('str_var', 'leaf', C_TYPE => 'const struct variable *',
	       ATOM => 'variable', MANGLE => 'Vs',
	       HUMAN_NAME => 'string_variable');
    init_type ('var', 'leaf', C_TYPE => 'const struct variable *',
	       ATOM => 'variable', MANGLE => 'V',
	       HUMAN_NAME => 'variable');

    # Vectors.
    init_type ('vector', 'leaf', C_TYPE => 'const struct vector *',
	       ATOM => 'vector', MANGLE => 'v', HUMAN_NAME => 'vector');

    # Fixed types.
    init_type ('expression', 'fixed', C_TYPE => 'struct expression *',
	       FIXED_VALUE => 'e');
    init_type ('case', 'fixed', C_TYPE => 'const struct ccase *',
	       FIXED_VALUE => 'c');
    init_type ('case_idx', 'fixed', C_TYPE => 'size_t',
	       FIXED_VALUE => 'case_idx');
    init_type ('dataset', 'fixed', C_TYPE => 'struct dataset *',
	       FIXED_VALUE => 'ds');

    # One of these is emitted at the end of each expression as a sentinel
    # that tells expr_evaluate() to return the value on the stack.
    init_type ('return_number', 'atom');
    init_type ('return_string', 'atom');

    # Used only for debugging purposes.
    init_type ('operation', 'atom');
}

# init_type has 2 required arguments:
#
#   NAME: Type name.
#
#           `$name' is the type's name in operations.def.
#
#           `OP_$name' is the terminal's type in operations.h.
#
#           `expr_allocate_$name()' allocates a node of the given type.
#
#   ROLE: How the type may be used:
#
#           "any": Usable as operands and function arguments, and
#           function and operator results.
#
#           "leaf": Usable as operands and function arguments, but
#           not function arguments or results.  (Thus, they appear
#           only in leaf nodes in the parse type.)
#
#           "fixed": Not allowed either as an operand or argument
#           type or a result type.  Used only as auxiliary data.
#
#           "atom": Not allowed anywhere; just adds the name to
#           the list of atoms.
#
# All types except those with "atom" as their role also require:
#
#   C_TYPE: The C type that represents this abstract type.
#
# Types with "any" or "leaf" role require:
#
#   ATOM:
#
#           `$atom' is the `struct operation_data' member name.
#
#           get_$atom_name() obtains the corresponding data from a
#           node.
#
#   MANGLE: Short string for name mangling.  Use identical strings
#   if two types should not be overloaded.
#
#   HUMAN_NAME: Name for a type when we describe it to the user.
#
# Types with role "any" require:
#
#   STACK: Name of the local variable in expr_evaluate(), used for
#   maintaining the stack for this type.
#
#   MISSING_VALUE: Expression used for the missing value of this
#   type.
#
# Types with role "fixed" require:
#
#   FIXED_VALUE: Expression used for the value of this type.
sub init_type {
    my ($name, $role, %rest) = @_;
    my ($type) = $type{"\U$name"} = {NAME => $name, ROLE => $role, %rest};

    my (@need_keys) = qw (NAME ROLE);
    if ($role eq 'any') {
	push (@need_keys, qw (C_TYPE ATOM MANGLE HUMAN_NAME STACK MISSING_VALUE));
    } elsif ($role eq 'leaf') {
	push (@need_keys, qw (C_TYPE ATOM MANGLE HUMAN_NAME));
    } elsif ($role eq 'fixed') {
	push (@need_keys, qw (C_TYPE FIXED_VALUE));
    } elsif ($role eq 'atom') {
    } else {
	die "no role `$role'";
    }

    my (%have_keys);
    $have_keys{$_} = 1 foreach keys %$type;
    for my $key (@need_keys) {
	defined $type->{$key} or die "$name lacks $key";
	delete $have_keys{$key};
    }
    scalar (keys (%have_keys)) == 0
      or die "$name has superfluous key(s) " . join (', ', keys (%have_keys));

    push (@types, $type);
}

# c_type(type).
#
# Returns the C type of the given type as a string designed to be
# prepended to a variable name to produce a declaration.  (That won't
# work in general but it works well enough for our types.)
sub c_type {
    my ($type) = @_;
    my ($c_type) = $type->{C_TYPE};
    defined $c_type or die;

    # Append a space unless (typically) $c_type ends in `*'.
    $c_type .= ' ' if $c_type =~ /\w$/;

    return $c_type;
}

# Input parsing.

# Parses the entire input.
#
# Initializes %ops, @funcs, @opers.
sub parse_input {
    get_line ();
    get_token ();
    while ($toktype ne 'eof') {
	my (%op);

	$op{OPTIMIZABLE} = 1;
	$op{UNIMPLEMENTED} = 0;
	$op{EXTENSION} = 0;
	$op{PERM_ONLY} = 0;
	for (;;) {
	    if (match ('extension')) {
		$op{EXTENSION} = 1;
	    } elsif (match ('no_opt')) {
		$op{OPTIMIZABLE} = 0;
	    } elsif (match ('absorb_miss')) {
		$op{ABSORB_MISS} = 1;
	    } elsif (match ('perm_only')) {
		$op{PERM_ONLY} = 1;
	    } elsif (match ('no_abbrev')) {
		$op{NO_ABBREV} = 1;
	    } else {
		last;
	    }
	}

	$op{RETURNS} = parse_type () || $type{NUMBER};
	die "$op{RETURNS} is not a valid return type"
	  if !any ($op{RETURNS}, @type{qw (NUMBER STRING BOOLEAN)});

	$op{CATEGORY} = $token;
	if (!any ($op{CATEGORY}, qw (operator function))) {
	    die "`operator' or `function' expected at `$token'";
	}
	get_token ();

	my ($name) = force ("id");

	die "function name may not contain underscore"
	  if $op{CATEGORY} eq 'function' && $name =~ /_/;
	die "operator name may not contain period"
	  if $op{CATEGORY} eq 'operator' && $name =~ /\./;

	if (my ($prefix, $suffix) = $name =~ /^(.*)\.(\d+)$/) {
	    $name = $prefix;
	    $op{MIN_VALID} = $suffix;
	    $op{ABSORB_MISS} = 1;
	}
	$op{NAME} = $name;

	force_match ('(');
	@{$op{ARGS}} = ();
	while (!match (')')) {
	    my ($arg) = parse_arg ();
	    push (@{$op{ARGS}}, $arg);
	    if (defined ($arg->{IDX})) {
		last if match (')');
		die "array must be last argument";
	    }
	    if (!match (',')) {
		force_match (')');
		last;
	    }
	}

	for my $arg (@{$op{ARGS}}) {
	    next if !defined $arg->{CONDITION};
	    my ($any_arg) = join ('|', map ($_->{NAME}, @{$op{ARGS}}));
	    $arg->{CONDITION} =~ s/\b($any_arg)\b/arg_$1/g;
	}

	my ($opname) = "OP_$op{NAME}";
	$opname =~ tr/./_/;
	if ($op{CATEGORY} eq 'function') {
	    my ($mangle) = join ('', map ($_->{TYPE}{MANGLE}, @{$op{ARGS}}));
	    $op{MANGLE} = $mangle;
	    $opname .= "_$mangle";
	}
	$op{OPNAME} = $opname;

	if ($op{MIN_VALID}) {
	    my ($array_arg) = array_arg (\%op);
	    die "can't have minimum valid count without array arg"
	      if !defined $array_arg;
	    die "minimum valid count allowed only with double array"
	      if $array_arg->{TYPE} ne $type{NUMBER};
	    die "can't have minimum valid count if array has multiplication factor"
	      if $array_arg->{TIMES} != 1;
	}

	while ($toktype eq 'id') {
	    my ($type) = parse_type () or die "parse error";
	    die "`$type->{NAME}' is not allowed as auxiliary data"
	      unless $type->{ROLE} eq 'leaf' || $type->{ROLE} eq 'fixed';
	    my ($name) = force ("id");
	    push (@{$op{AUX}}, {TYPE => $type, NAME => $name});
	    force_match (';');
	}

	if ($op{OPTIMIZABLE}) {
	    die "random variate functions must be marked `no_opt'"
	      if $op{NAME} =~ /^RV\./;
	    for my $aux (@{$op{AUX}}) {
		if (any ($aux->{TYPE}, @type{qw (CASE CASE_IDX)})) {
		    die "operators with $aux->{TYPE} aux data must be "
		      . "marked `no_opt'";
		}
	    }
	}

	if ($op{RETURNS} eq $type{STRING} && !defined ($op{ABSORB_MISS})) {
	    my (@args);
	    for my $arg (@{$op{ARGS}}) {
		if (any ($arg->{TYPE}, @type{qw (NUMBER BOOLEAN)})) {
		    die "$op{NAME} returns string and has double or bool "
		      . "argument, but is not marked ABSORB_MISS";
		}
		if (defined $arg->{CONDITION}) {
		    die "$op{NAME} returns string but has argument with condition";
		}
	    }
	}

	if ($toktype eq 'block') {
	    $op{BLOCK} = force ('block');
	} elsif ($toktype eq 'expression') {
	    if ($token eq 'unimplemented') {
		$op{UNIMPLEMENTED} = 1;
	    } else {
		$op{EXPRESSION} = $token;
	    }
	    get_token ();
	} else {
	    die "block or expression expected";
	}

	die "duplicate operation name $opname" if defined $ops{$opname};
	$ops{$opname} = \%op;
	if ($op{CATEGORY} eq 'function') {
	    push (@funcs, $opname);
	} else {
	    push (@opers, $opname);
	}
    }
    close(INPUT);

    @funcs = sort {$ops{$a}->{NAME} cmp $ops{$b}->{NAME}
		     ||
		       $ops{$a}->{OPNAME} cmp $ops{$b}->{OPNAME}}
      @funcs;
    @opers = sort {$ops{$a}->{NAME} cmp $ops{$b}->{NAME}} @opers;
    @order = (@funcs, @opers);
}

# Reads the next token into $token, $toktype.
sub get_token {
    our ($line);
    lookahead ();
    return if defined ($toktype) && $toktype eq 'eof';
    $toktype = 'id', $token = $1, return
	if $line =~ /\G([a-zA-Z_][a-zA-Z_.0-9]*)/gc;
    $toktype = 'int', $token = $1, return if $line =~ /\G([0-9]+)/gc;
    $toktype = 'punct', $token = $1, return if $line =~ /\G([][(),*;.])/gc;
    if ($line =~ /\G=/gc) {
	$toktype = "expression";
	$line =~ /\G\s+/gc;
	$token = accumulate_balanced (';');
    } elsif ($line =~ /\G\{/gc) {
	$toktype = "block";
	$token = accumulate_balanced ('}');
	$token =~ s/^\n+//;
    } else {
	die "bad character `" . substr ($line, pos $line, 1) . "' in input";
    }
}

# Skip whitespace, then return the remainder of the line.
sub lookahead {
    our ($line);
    die "unexpected end of file" if !defined ($line);
    for (;;) {
	$line =~ /\G\s+/gc;
	last if pos ($line) < length ($line);
	get_line ();
	$token = $toktype = 'eof', return if !defined ($line);
    }
    return substr ($line, pos ($line));
}

# accumulate_balanced($chars)
#
# Accumulates input until a character in $chars is encountered, except
# that balanced pairs of (), [], or {} cause $chars to be ignored.
#
# Returns the input read.
sub accumulate_balanced {
    my ($end) = @_;
    my ($s) = "";
    my ($nest) = 0;
    our ($line);
    for (;;) {
	my ($start) = pos ($line);
	if ($line =~ /\G([^][(){};,]*)([][(){};,])/gc) {
	    $s .= substr ($line, $start, pos ($line) - $start - 1)
		if pos ($line) > $start;
	    my ($last) = substr ($line, pos ($line) - 1, 1);
	    if ($last =~ /[[({]/) {
		$nest++;
		$s .= $last;
	    } elsif ($last =~ /[])}]/) {
		if ($nest > 0) {
		    $nest--;
		    $s .= $last;
		} elsif (index ($end, $last) >= 0) {
		    return $s;
		} else {
		    die "unbalanced parentheses";
		}
	    } elsif (index ($end, $last) >= 0) {
		return $s if !$nest;
		$s .= $last;
	    } else {
		$s .= $last;
	    }
	} else {
	    $s .= substr ($line, pos ($line)) . "\n";
	    get_line ();
	}
    }
}

# Reads the next line from INPUT into $line.
sub get_line {
    our ($line);
    $line = <INPUT>;
    if (defined ($line)) {
	chomp $line;
	$line =~ s%//.*%%;
	pos ($line) = 0;
    }
}

# If the current token is an identifier that names a type,
# returns the type and skips to the next token.
# Otherwise, returns undef.
sub parse_type {
    if ($toktype eq 'id') {
	foreach my $type (values (%type)) {
	    get_token (), return $type
	      if defined ($type->{NAME}) && $type->{NAME} eq $token;
	}
    }
    return;
}

# force($type).
#
# Makes sure that $toktype equals $type, reads the next token, and
# returns the previous $token.
sub force {
    my ($type) = @_;
    die "parse error at `$token' expecting $type"
	if $type ne $toktype;
    my ($tok) = $token;
    get_token ();
    return $tok;
}

# force($tok).
#
# If $token equals $tok, reads the next token and returns true.
# Otherwise, returns false.
sub match {
    my ($tok) = @_;
    if ($token eq $tok) {
	get_token ();
	return 1;
    } else {
	return 0;
    }
}

# force_match($tok).
#
# If $token equals $tok, reads the next token.
# Otherwise, flags an error in the input.
sub force_match {
    my ($tok) = @_;
    die "parse error at `$token' expecting `$tok'" if !match ($tok);
}

# Parses and returns a function argument.
sub parse_arg {
    my (%arg);
    $arg{TYPE} = parse_type () || $type{NUMBER};
    die "argument name expected at `$token'" if $toktype ne 'id';
    $arg{NAME} = $token;

    if (lookahead () =~ /^[[,)]/) {
	get_token ();
	if (match ('[')) {
	    die "only double and string arrays supported"
	      if !any ($arg{TYPE}, @type{qw (NUMBER STRING)});
	    $arg{IDX} = force ('id');
	    if (match ('*')) {
		$arg{TIMES} = force ('int');
		die "multiplication factor must be positive"
		  if $arg{TIMES} < 1;
	    } else {
		$arg{TIMES} = 1;
	    }
	    force_match (']');
	}
    } else {
	$arg{CONDITION} = $arg{NAME} . ' ' . accumulate_balanced (',)');
	our ($line);
	pos ($line) -= 1;
	get_token ();
    }
    return \%arg;
}

# Output.

# Prints the output file header.
sub print_header {
    print <<EOF;
/* $output_file
   Generated from $input_file by generate.pl.  
   Do not modify! */

EOF
}

# Prints the output file trailer.
sub print_trailer {
    print <<EOF;

/*
   Local Variables:
   mode: c
   buffer-read-only: t
   End:
*/
EOF
}

sub generate_evaluate_h {
    print "#include \"helpers.h\"\n\n";

    for my $opname (@order) {
	my ($op) = $ops{$opname};
	next if $op->{UNIMPLEMENTED};

	my (@args);
	for my $arg (@{$op->{ARGS}}) {
	    if (!defined $arg->{IDX}) {
		push (@args, c_type ($arg->{TYPE}) . $arg->{NAME});
	    } else {
		push (@args, c_type ($arg->{TYPE}) . "$arg->{NAME}" . "[]");
		push (@args, "size_t $arg->{IDX}");
	    }
	}
	for my $aux (@{$op->{AUX}}) {
	    push (@args, c_type ($aux->{TYPE}) . $aux->{NAME});
	}
	push (@args, "void") if !@args;

	my ($statements) = $op->{BLOCK} || "  return $op->{EXPRESSION};\n";

	print "static inline ", c_type ($op->{RETURNS}), "\n";
	print "eval_$opname (", join (', ', @args), ")\n";
	print "{\n";
	print "$statements";
	print "}\n\n";
    }
}

sub generate_evaluate_inc {
    for my $opname (@order) {
	my ($op) = $ops{$opname};

	if ($op->{UNIMPLEMENTED}) {
	    print "case $opname:\n";
	    print "  NOT_REACHED ();\n\n";
	    next;
	}

	my (@decls);
	my (@args);
	for my $arg (@{$op->{ARGS}}) {
	    my ($name) = $arg->{NAME};
	    my ($type) = $arg->{TYPE};
	    my ($c_type) = c_type ($type);
	    my ($idx) = $arg->{IDX};
	    push (@args, "arg_$arg->{NAME}");
	    if (!defined ($idx)) {
		my ($decl) = "${c_type}arg_$name";
		if ($type->{ROLE} eq 'any') {
		    unshift (@decls, "$decl = *--$type->{STACK}");
		} elsif ($type->{ROLE} eq 'leaf') {
		    push (@decls, "$decl = op++->$type->{ATOM}");
		} else {
		    die;
		}
	    } else {
		my ($stack) = $type->{STACK};
		defined $stack or die;
		unshift (@decls,
			 "$c_type*arg_$arg->{NAME} = $stack -= arg_$idx");
		unshift (@decls, "size_t arg_$arg->{IDX} = op++->integer");

		my ($idx) = "arg_$idx";
		if ($arg->{TIMES} != 1) {
		    $idx .= " / $arg->{TIMES}";
		}
		push (@args, $idx);
	    }
	}
	for my $aux (@{$op->{AUX}}) {
	    my ($type) = $aux->{TYPE};
	    my ($name) = $aux->{NAME};
	    if ($type->{ROLE} eq 'leaf') {
		my ($c_type) = c_type ($type);
		push (@decls, "${c_type}aux_$name = op++->$type->{ATOM}");
		push (@args, "aux_$name");
	    } elsif ($type->{ROLE} eq 'fixed') {
		push (@args, $type->{FIXED_VALUE});
	    }
	}

	my ($sysmis_cond) = make_sysmis_decl ($op, "op++->integer");
	push (@decls, $sysmis_cond) if defined $sysmis_cond;

	my ($result) = "eval_$op->{OPNAME} (" . join (', ', @args) . ")";

	my ($stack) = $op->{RETURNS}{STACK};

	print "case $opname:\n";
	if (@decls) {
	    print "  {\n";
	    print "    $_;\n" foreach @decls;
	    if (defined $sysmis_cond) {
		my ($miss_ret) = $op->{RETURNS}{MISSING_VALUE};
		print "    *$stack++ = force_sysmis ? $miss_ret : $result;\n";
	    } else {
		print "    *$stack++ = $result;\n";
	    }
	    print "  }\n";
	} else {
	    print "  *$stack++ = $result;\n";
	}
	print "  break;\n\n";
    }
}

sub generate_operations_h {
    print "#include <stdlib.h>\n";
    print "#include <stdbool.h>\n\n";

    print "typedef enum";
    print "  {\n";
    my (@atoms);
    foreach my $type (@types) {
	next if $type->{ROLE} eq 'fixed';
	push (@atoms, "OP_$type->{NAME}");
    }
    print_operations ('atom', 1, \@atoms);
    print_operations ('function', "OP_atom_last + 1", \@funcs);
    print_operations ('operator', "OP_function_last + 1", \@opers);
    print_range ("OP_composite", "OP_function_first", "OP_operator_last");
    print ",\n\n";
    print_range ("OP", "OP_atom_first", "OP_composite_last");
    print "\n  }\n";
    print "operation_type, atom_type;\n";

    print_predicate ('is_operation', 'OP');
    print_predicate ("is_$_", "OP_$_")
	foreach qw (atom composite function operator);
}

sub print_operations {
    my ($type, $first, $names) = @_;
    print "    /* \u$type types. */\n";
    print "    $names->[0] = $first,\n";
    print "    $_,\n" foreach @$names[1...$#{$names}];
    print_range ("OP_$type", $names->[0], $names->[$#{$names}]);
    print ",\n\n";
}

sub print_range {
    my ($prefix, $first, $last) = @_;
    print "    ${prefix}_first = $first,\n";
    print "    ${prefix}_last = $last,\n";
    print "    ${prefix}_cnt = ${prefix}_last - ${prefix}_first + 1";
}

sub print_predicate {
    my ($function, $category) = @_;
    my ($assertion) = "";

    print "\nstatic inline bool\n";
    print "$function (operation_type op)\n";
    print "{\n";
    print "  assert (is_operation (op));\n" if $function ne 'is_operation';
    print "  return op >= ${category}_first && op <= ${category}_last;\n";
    print "}\n";
}

sub generate_optimize_inc {
    for my $opname (@order) {
	my ($op) = $ops{$opname};

	if (!$op->{OPTIMIZABLE} || $op->{UNIMPLEMENTED}) {
	    print "case $opname:\n";
	    print "  NOT_REACHED ();\n\n";
	    next;
	}

	my (@decls);
	my ($arg_idx) = 0;
	for my $arg (@{$op->{ARGS}}) {
	    my ($decl);
	    my ($name) = $arg->{NAME};
	    my ($type) = $arg->{TYPE};
	    my ($ctype) = c_type ($type);
	    my ($idx) = $arg->{IDX};
	    if (!defined ($idx)) {
		my ($func) = "get_$type->{ATOM}_arg";
		push (@decls, "${ctype}arg_$name = $func (node, $arg_idx)");
	    } else {
		my ($decl) = "size_t arg_$idx = node->arg_cnt";
		$decl .= " - $arg_idx" if $arg_idx;
		push (@decls, $decl);

		push (@decls, "${ctype}*arg_$name = "
		      . "get_$type->{ATOM}_args "
		      . " (node, $arg_idx, arg_$idx, e)");
	    }
	    $arg_idx++;
	}

	my ($sysmis_cond) = make_sysmis_decl ($op, "node->min_valid");
	push (@decls, $sysmis_cond) if defined $sysmis_cond;

	my (@args);
	for my $arg (@{$op->{ARGS}}) {
	    push (@args, "arg_$arg->{NAME}");
	    if (defined $arg->{IDX}) {
		my ($idx) = "arg_$arg->{IDX}";
		$idx .= " / $arg->{TIMES}" if $arg->{TIMES} != 1;
		push (@args, $idx);
	    }
	}
	for my $aux (@{$op->{AUX}}) {
	    my ($type) = $aux->{TYPE};
	    if ($type->{ROLE} eq 'leaf') {
		my ($func) = "get_$type->{ATOM}_arg";
		push (@args, "$func (node, $arg_idx)");
		$arg_idx++;
	    } elsif ($type->{ROLE} eq 'fixed') {
		push (@args, $type->{FIXED_VALUE});
	    } else {
		die;
	    }
	}

	my ($result) = "eval_$op->{OPNAME} (" . join (', ', @args) . ")";
	if (@decls && defined ($sysmis_cond)) {
	    my ($miss_ret) = $op->{RETURNS}{MISSING_VALUE};
	    push (@decls, c_type ($op->{RETURNS}) . "result = "
		  . "force_sysmis ? $miss_ret : $result");
	    $result = "result";
	}

	print "case $opname:\n";
	my ($alloc_func) = "expr_allocate_$op->{RETURNS}{NAME}";
	if (@decls) {
	    print "  {\n";
	    print "    $_;\n" foreach @decls;
	    print "    return $alloc_func (e, $result);\n";
	    print "  }\n";
	} else {
	    print "  return $alloc_func (e, $result);\n";
	}
	print "\n";
    }
}

sub generate_parse_inc {
    my (@members) = ("\"\"", "\"\"", 0, 0, 0, "{}", 0, 0);
    print "{", join (', ', @members), "},\n";

    for my $type (@types) {
	next if $type->{ROLE} eq 'fixed';

	my ($human_name) = $type->{HUMAN_NAME};
	$human_name = $type->{NAME} if !defined $human_name;

	my (@members) = ("\"$type->{NAME}\"", "\"$human_name\"",
			 0, "OP_$type->{NAME}", 0, "{}", 0, 0);
	print "{", join (', ', @members), "},\n";
    }

    for my $opname (@order) {
	my ($op) = $ops{$opname};

	my (@members);

	push (@members, "\"$op->{NAME}\"");

	if ($op->{CATEGORY} eq 'function') {
	    my (@args, @opt_args);
	    for my $arg (@{$op->{ARGS}}) {
		push (@args, $arg->{TYPE}{HUMAN_NAME}) if !defined $arg->{IDX};
	    }

	    if (my ($array) = array_arg ($op)) {
		if (!defined $op->{MIN_VALID}) {
		    my (@array_args);
		    for (my $i = 0; $i < $array->{TIMES}; $i++) {
			push (@array_args, $array->{TYPE}{HUMAN_NAME});
		    }
		    push (@args, @array_args);
		    @opt_args = @array_args;
		} else {
		    for (my $i = 0; $i < $op->{MIN_VALID}; $i++) {
			push (@args, $array->{TYPE}{HUMAN_NAME});
		    }
		    push (@opt_args, $array->{TYPE}{HUMAN_NAME});
		}
	    }
	    my ($human) = "$op->{NAME}(" . join (', ', @args);
	    $human .= '[, ' . join (', ', @opt_args) . ']...' if @opt_args;
	    $human .= ')';
	    push (@members, "\"$human\"");
	} else {
	    push (@members, "NULL");
	}

	my (@flags);
	push (@flags, "OPF_ABSORB_MISS") if defined $op->{ABSORB_MISS};
	push (@flags, "OPF_ARRAY_OPERAND") if array_arg ($op);
	push (@flags, "OPF_MIN_VALID") if defined $op->{MIN_VALID};
	push (@flags, "OPF_NONOPTIMIZABLE") if !$op->{OPTIMIZABLE};
	push (@flags, "OPF_EXTENSION") if $op->{EXTENSION};
	push (@flags, "OPF_UNIMPLEMENTED") if $op->{UNIMPLEMENTED};
	push (@flags, "OPF_PERM_ONLY") if $op->{PERM_ONLY};
	push (@flags, "OPF_NO_ABBREV") if $op->{NO_ABBREV};
	push (@members, @flags ? join (' | ', @flags) : 0);

	push (@members, "OP_$op->{RETURNS}{NAME}");

	push (@members, scalar (@{$op->{ARGS}}));

	my (@arg_types) = map ("OP_$_->{TYPE}{NAME}", @{$op->{ARGS}});
	push (@members, "{" . join (', ', @arg_types) . "}");

	push (@members, $op->{MIN_VALID} || 0);

	push (@members, array_arg ($op) ? ${array_arg ($op)}{TIMES} : 0);

	print "{", join (', ', @members), "},\n";
    }
}

# Utilities.

# any($target, @list)
#
# Returns true if $target appears in @list,
# false otherwise.
sub any {
    $_ eq $_[0] and return 1 foreach @_[1...$#_];
    return 0;
}

# make_sysmis_decl($op, $min_valid_src)
#
# Returns a declaration for a boolean variable called `force_sysmis',
# which will be true when operation $op should be system-missing.
# Returns undef if there are no such circumstances.
#
# If $op has a minimum number of valid arguments, $min_valid_src
# should be an an expression that evaluates to the minimum number of
# valid arguments for $op.
sub make_sysmis_decl {
    my ($op, $min_valid_src) = @_;
    my (@sysmis_cond); 
    if (!$op->{ABSORB_MISS}) {
	for my $arg (@{$op->{ARGS}}) {
	    my ($arg_name) = "arg_$arg->{NAME}";
	    if (!defined $arg->{IDX}) {
		if (any ($arg->{TYPE}, @type{qw (NUMBER BOOLEAN)})) {
		    push (@sysmis_cond, "!is_valid ($arg_name)");
		}
	    } elsif ($arg->{TYPE} eq $type{NUMBER}) {
		my ($a) = "$arg_name";
		my ($n) = "arg_$arg->{IDX}";
		push (@sysmis_cond, "count_valid ($a, $n) < $n");
	    }
	}
    } elsif (defined $op->{MIN_VALID}) {
	my ($args) = $op->{ARGS};
	my ($arg) = ${$args}[$#{$args}];
	my ($a) = "arg_$arg->{NAME}";
	my ($n) = "arg_$arg->{IDX}";
	push (@sysmis_cond, "count_valid ($a, $n) < $min_valid_src");
    }
    for my $arg (@{$op->{ARGS}}) {
	push (@sysmis_cond, "!($arg->{CONDITION})")
	  if defined $arg->{CONDITION};
    }
    return "bool force_sysmis = " . join (' || ', @sysmis_cond)
      if @sysmis_cond;
    return;
}

# array_arg($op)
#
# If $op has an array argument, return it.
# Otherwise, returns undef.
sub array_arg {
    my ($op) = @_;
    my ($args) = $op->{ARGS};
    return if !@$args;
    my ($last_arg) = $args->[@$args - 1];
    return $last_arg if defined $last_arg->{IDX};
    return;
}
