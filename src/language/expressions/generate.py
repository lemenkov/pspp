#! /usr/bin/python3
# PSPP - a program for statistical analysis.
# Copyright (C) 2017, 2021 Free Software Foundation, Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
import enum
import getopt
import re
import sys

argv0 = sys.argv[0]


def die(s):
    sys.stderr.write("%s\n" % s)
    sys.exit(1)


def init_all_types():
    """Defines all our types.

    Initializes 'types' global.

    """

    global types
    types = {}

    for t in [
        # Common user-visible types used throughout evaluation trees.
        Type.new_any('number', 'double', 'number', 'n',
                     'number', 'ns', 'SYSMIS'),
        Type.new_any('string', 'struct substring', 'string', 's',
                     'string', 'ss', 'empty_string'),
        Type.new_any('boolean', 'double', 'number', 'n',
                     'boolean', 'ns', 'SYSMIS'),
        Type.new_any('integer', 'int', 'number', 'n',
                     'integer', 'ns', 'SYSMIS'),

        # Format types.
        Type.new_atom('format'),
        Type.new_leaf('ni_format', 'struct fmt_spec',
                      'format', 'f', 'num_input_format'),
        Type.new_leaf('no_format', 'struct fmt_spec',
                      'format', 'f', 'num_output_format'),

        # Integer types.
        Type.new_leaf('pos_int', 'int', 'integer', 'n',
                      'positive_integer_constant'),

        # Variable names.
        Type.new_atom('variable'),
        Type.new_leaf('num_var', 'const struct variable *',
                      'variable', 'Vn', 'num_variable'),
        Type.new_leaf('str_var', 'const struct variable *',
                      'variable', 'Vs', 'string_variable'),
        Type.new_leaf('var', 'const struct variable *',
                      'variable', 'V', 'variable'),

        # Vectors.
        Type.new_leaf('vector', 'const struct vector *',
                      'vector', 'v', 'vector'),
        Type.new_any('num_vec_elem', 'double', 'number', 'n',
                     'number', 'ns', 'SYSMIS'),

        # Types as leaves or auxiliary data.
        Type.new_leaf('expr_node', 'const struct expr_node *',
                      'expr_node', 'e', 'expr_node'),

        # Types that appear only as auxiliary data.
        Type.new_auxonly('expression', 'struct expression *', 'e'),
        Type.new_auxonly('case', 'const struct ccase *', 'c'),
        Type.new_auxonly('case_idx', 'size_t', 'case_idx'),
        Type.new_auxonly('dataset', 'struct dataset *', 'ds'),

        # One of these is emitted at the end of each expression as a
        # sentinel that tells expr_evaluate() to return the value on
        # the stack.
        Type.new_atom('return_number'),
        Type.new_atom('return_string'),

        # Used only for debugging purposes.
        Type.new_atom('operation'),
    ]:
        types[t.name] = t


class Type:
    def __init__(self, name, role, human_name, c_type=None):
        self.name = name
        self.role = role
        self.human_name = human_name
        if c_type:
            if c_type.endswith('*'):
                self.c_type = c_type
            else:
                self.c_type = c_type + ' '

    def new_atom(name):
        """Creates and returns a new atom Type with the given 'name'.

        An atom isn't directly allowed as an operand or function
        argument type.  They are all exceptional cases in some way.

        """
        return Type(name, 'atom', name)

    def new_any(name, c_type, atom, mangle, human_name, stack,
                missing_value):
        """Creates and returns a new Type that can appear in any context, that
        is, it can be an operation's argument type or return type.

        'c_type' is the type used for C objects of this type.

        'atom' should be the name of the member of "union
        operation_data" that holds a value of this type.

        'mangle' should be a short string for name mangling purposes,
        to allow overloading functions with the same name but
        different argument types.  Use the same 'mangle' for two
        different types if those two types should not be overloaded.

        'human_name' should be a name to use when describing this type
        to the user (see Op.prototype()).

        'stack' is the name of the local variable in expr_evaluate()
        used for maintaining a stack of this type.

        'missing_value' is the expression used for a missing value of
        this type.

        """
        new = Type(name, 'any', human_name, c_type)
        new.atom = atom
        new.mangle = mangle
        new.stack = stack
        new.missing_value = missing_value
        return new

    def new_leaf(name, c_type, atom, mangle, human_name):
        """Creates and returns a new leaf Type.  A leaf type can appear in
        expressions as an operation's argument type, but not as a return type.
        (Thus, it only appears in a parse tree as a leaf node.)

        The other arguments are as for new_any().
        """
        new = Type(name, 'leaf', human_name, c_type)
        new.atom = atom
        new.mangle = mangle
        return new

    def new_auxonly(name, c_type, auxonly_value):
        """Creates and returns a new auxiliary-only Type.  An auxiliary-only
        Type is one that gets passed into the evaluation function but
        isn't supplied directly by the user as an operand or argument.

        'c_type' is as in new_any().

        'auxonly_value' is the name of the local variable in
        expr_evaluate() that has the value of this auxiliary data.

        """
        new = Type(name, 'auxonly', name, c_type)
        new.auxonly_value = auxonly_value
        return new

    def parse():
        """If the current token is an identifier that names a type, returns
        the type and skips to the next token.  Otherwise, returns
        None.
        """
        if toktype == 'id':
            for type_ in types.values():
                if type_.name == token:
                    get_token()
                    return type_
        return None


class Category(enum.Enum):
    FUNCTION = enum.auto()
    OPERATOR = enum.auto()


class Op:
    def __init__(self, name, category, returns, args, aux, expression,
                 block, min_valid, optimizable, unimplemented,
                 extension, perm_only, absorb_miss, no_abbrev):
        self.name = name
        self.category = category
        self.returns = returns
        self.args = args
        self.aux = aux
        self.expression = expression
        self.block = block
        self.min_valid = min_valid
        self.optimizable = optimizable
        self.unimplemented = unimplemented
        self.extension = extension
        self.perm_only = perm_only
        self.absorb_miss = absorb_miss
        self.no_abbrev = no_abbrev

        self.opname = ('OP_%s' % name).replace('.', '_')
        if category == Category.FUNCTION:
            self.opname += '_%s' % (''.join([a.type_.mangle for a in args]))

    def array_arg(self):
        """If this operation has an array argument, returns it.  Otherwise,
        returns None.
        """
        if self.args and self.args[-1].idx is not None:
            return self.args[-1]
        else:
            return None

    def sysmis_decl(self, min_valid_src):
        """Returns a declaration for a boolean variable called `force_sysmis',
        which will be true when this operation should be
        system-missing.  Returns None if there are no such
        circumstances.

        If this operation has a minimum number of valid arguments,
        'min_valid_src' should be an expression that evaluates to
        the minimum number of valid arguments for this operation.

        """
        sysmis_cond = []
        if not self.absorb_miss:
            for arg in self.args:
                arg_name = 'arg_%s' % arg.name
                if arg.idx is None:
                    if arg.type_.name in ['number', 'boolean', 'integer']:
                        sysmis_cond += ['!is_valid (%s)' % arg_name]
                elif arg.type_.name == 'number':
                    a = arg_name
                    n = 'arg_%s' % arg.idx
                    sysmis_cond += ['count_valid (%s, %s) < %s' % (a, n, n)]
        elif self.min_valid > 0:
            args = self.args
            arg = args[-1]
            a = 'arg_%s' % arg.name
            n = 'arg_%s' % arg.idx
            sysmis_cond += ['count_valid (%s, %s) < %s'
                            % (a, n, min_valid_src)]
        for arg in self.args:
            if arg.condition is not None:
                sysmis_cond += ['!(%s)' % arg.condition]
        if sysmis_cond:
            return 'bool force_sysmis = %s' % ' || '.join(sysmis_cond)
        return None

    def prototype(self):
        """Composes and returns a string that describes the function in a way
        suitable for a human to understand, something like a C
        function prototype, e.g. "ABS(number)" or "ANY(number,
        number[, number]...)".

        This doesn't make sense for operators so this function just
        returns None for them.

        """
        if self.category == Category.FUNCTION:
            args = []
            opt_args = []
            for arg in self.args:
                if arg.idx is None:
                    args += [arg.type_.human_name]

            array = self.array_arg()
            if array is not None:
                if self.min_valid == 0:
                    array_args = []
                    for i in range(array.times):
                        array_args += [array.type_.human_name]
                    args += array_args
                    opt_args = array_args
                else:
                    for i in range(self.min_valid):
                        args += [array.type_.human_name]
                    opt_args += [array.type_.human_name]

            prototype = '%s(%s' % (self.name, ', '.join(args))
            if opt_args:
                prototype += '[, %s]...' % ', '.join(opt_args)
            prototype += ')'
            return prototype
        else:
            return None

    def flags(self):
        """Returns the OPF_* flags that apply to 'self'."""
        flags = []
        if self.absorb_miss:
            flags += ['OPF_ABSORB_MISS']
        if self.array_arg():
            flags += ['OPF_ARRAY_OPERAND']
        if self.min_valid > 0:
            flags += ['OPF_MIN_VALID']
        if not self.optimizable:
            flags += ['OPF_NONOPTIMIZABLE']
        if self.extension:
            flags += ['OPF_EXTENSION']
        if self.unimplemented:
            flags += ['OPF_UNIMPLEMENTED']
        if self.perm_only:
            flags += ['OPF_PERM_ONLY']
        if self.no_abbrev:
            flags += ['OPF_NO_ABBREV']
        for aux in self.aux:
            if aux['TYPE'].name == 'expr_node':
                flags += ['OPF_EXPR_NODE']
                break
        return ' | '.join(flags) if flags else '0'


def parse_input():
    """Parses the entire input.

    Initializes ops, funcs, opers."""

    global token
    global toktype
    global line_number
    token = None
    toktype = None
    line_number = 0
    get_line()
    get_token()

    global funcs
    global opers
    global order
    ops = {}
    funcs = []
    opers = []

    while toktype != 'eof':
        optimizable = True
        unimplemented = False
        extension = False
        perm_only = False
        absorb_miss = False
        no_abbrev = False
        while True:
            if match('extension'):
                extension = True
            elif match('no_opt'):
                optimizable = False
            elif match('absorb_miss'):
                absorb_miss = True
            elif match('perm_only'):
                perm_only = True
            elif match('no_abbrev'):
                no_abbrev = True
            else:
                break

        return_type = Type.parse()
        if return_type is None:
            return_type = types['number']
        if return_type.name not in ['number', 'string', 'boolean', 'num_vec_elem']:
            die('%s is not a valid return type' % return_type.name)

        if token == 'operator':
            category = Category.OPERATOR
        elif token == 'function':
            category = Category.FUNCTION
        else:
            die("'operator' or 'function' expected at '%s'" % token)
        get_token()

        name = force('id')
        if category == Category.FUNCTION and '_' in name:
            die("function name '%s' may not contain underscore" % name)
        elif category == Category.OPERATOR and '.' in name:
            die("operator name '%s' may not contain period" % name)

        m = re.match(r'(.*)\.(\d+)$', name)
        if m:
            prefix, suffix = m.groups()
            name = prefix
            min_valid = int(suffix)
            absorb_miss = True
        else:
            min_valid = 0

        force_match('(')
        args = []
        while not match(')'):
            arg = Arg.parse()
            args += [arg]
            if arg.idx is not None:
                if match(')'):
                    break
                die('array must be last argument')
            if not match(','):
                force_match(')')
                break

        for arg in args:
            if arg.condition is not None:
                any_arg = '|'.join([a.name for a in args])
                arg.condition = re.sub(r'\b(%s)\b' % any_arg,
                                       r'arg_\1', arg.condition)

        aux = []
        while toktype == 'id':
            type_ = Type.parse()
            if type_ is None:
                die('parse error')
            if type_.role not in ['leaf', 'auxonly']:
                die("'%s' is not allowed as auxiliary data" % type_.name)
            aux_name = force('id')
            aux += [{'TYPE': type_, 'NAME': aux_name}]
            force_match(';')

        if optimizable:
            if name.startswith('RV.'):
                die("random variate functions must be marked 'no_opt'")
            for key in ['CASE', 'CASE_IDX']:
                if key in aux:
                    die("operators with %s aux data must be marked 'no_opt'"
                        % key)

        if return_type.name == 'string' and not absorb_miss:
            for arg in args:
                if arg.type_.name in ['number', 'boolean']:
                    die("'%s' returns string and has double or bool "
                        "argument, but is not marked ABSORB_MISS" % name)
                if arg.condition is not None:
                    die("'%s' returns string but has "
                        "argument with condition")

        if toktype == 'block':
            block = force('block')
            expression = None
        elif toktype == 'expression':
            if token == 'unimplemented':
                unimplemented = True
            else:
                expression = token
            block = None
            get_token()
        else:
            die('block or expression expected')

        op = Op(name, category,
                return_type, args, aux,
                expression, block,
                min_valid,
                optimizable, unimplemented, extension, perm_only, absorb_miss,
                no_abbrev)

        if min_valid > 0:
            aa = op.array_arg()
            if aa is None:
                die("can't have minimum valid count without array arg")
            if aa.type_.name != 'number':
                die('minimum valid count allowed only with double array')
            if aa.times != 1:
                die("can't have minimum valid count if "
                    "array has multiplication factor")

        if op.opname in ops:
            die("duplicate operation name '%s'" % op.opname)
        ops[op.opname] = op
        if category == Category.FUNCTION:
            funcs += [op]
        else:
            opers += [op]

    in_file.close()

    funcs = sorted(funcs, key=lambda f: (f.name, f.opname))
    opers = sorted(opers, key=lambda o: o.name)
    order = funcs + opers


def get_token():
    """Reads the next token into 'token' and 'toktype'."""

    global line
    global token
    global toktype

    lookahead()
    if toktype == 'eof':
        return

    m = re.match(r'([a-zA-Z_][a-zA-Z_.0-9]*)(.*)$', line)
    if m:
        token, line = m.groups()
        toktype = 'id'
        return

    m = re.match(r'([0-9]+)(.*)$', line)
    if m:
        token, line = m.groups()
        token = int(token)
        toktype = 'int'
        return

    m = re.match(r'([][(),*;.])(.*)$', line)
    if m:
        token, line = m.groups()
        toktype = 'punct'
        return

    m = re.match(r'=\s*(.*)$', line)
    if m:
        toktype = 'expression'
        line = m.group(1)
        token = accumulate_balanced(';')
        return

    m = re.match(r'{(.*)$', line)
    if m:
        toktype = 'block'
        line = m.group(1)
        token = accumulate_balanced('}')
        token = token.rstrip('\n')
        return

    die("bad character '%s' in input" % line[0])


def lookahead():
    """Skip whitespace."""
    global line
    if line is None:
        die('unexpected end of file')

    while True:
        line = line.lstrip()
        if line != '':
            break
        get_line()
        if line is None:
            global token
            global toktype
            token = 'eof'
            toktype = 'eof'
            return


def accumulate_balanced(end, swallow_end=True):
    """Accumulates input until a character in 'end' is encountered,
    except that balanced pairs of (), [], or {} cause 'end' to be
    ignored.  Returns the input read.
    """
    s = ''
    nest = 0
    global line
    while True:
        for idx, c in enumerate(line):
            if c in end and nest == 0:
                line = line[idx:]
                if swallow_end:
                    line = line[1:]
                s = s.strip('\r\n')
                return s
            elif c in '[({':
                nest += 1
            elif c in '])}':
                if nest > 0:
                    nest -= 1
                else:
                    die('unbalanced parentheses')
            s += c
        s += '\n'
        get_line()


def get_line():
    """Reads the next line from INPUT into 'line'."""
    global line
    global line_number
    line = in_file.readline()
    line_number += 1
    if line == '':
        line = None
    else:
        line = line.rstrip('\r\n')
        comment_ofs = line.find('//')
        if comment_ofs >= 0:
            line = line[:comment_ofs]


def force(type_):
    """Makes sure that 'toktype' equals 'type', reads the next token, and
    returns the previous 'token'.

    """
    if type_ != toktype:
        die("parse error at `%s' expecting %s" % (token, type_))
    tok = token
    get_token()
    return tok


def match(tok):
    """If 'token' equals 'tok', reads the next token and returns true.
    Otherwise, returns false."""
    if token == tok:
        get_token()
        return True
    else:
        return False


def force_match(tok):
    """If 'token' equals 'tok', reads the next token.  Otherwise, flags an
    error in the input.
    """
    if not match(tok):
        die("parse error at `%s' expecting `%s'" % (token, tok))


class Arg:
    def __init__(self, name, type_, idx, times, condition):
        self.name = name
        self.type_ = type_
        self.idx = idx
        self.times = times
        self.condition = condition

    def parse():
        """Parses and returns a function argument."""
        type_ = Type.parse()
        if type_ is None:
            type_ = types['number']

        if toktype != 'id':
            die("argument name expected at `%s'" % token)
        name = token

        lookahead()
        global line

        idx = None
        times = 1

        if line[0] in '[,)':
            get_token()
            if match('['):
                if type_.name not in ('number', 'string'):
                    die('only double and string arrays supported')
                idx = force('id')
                if match('*'):
                    times = force('int')
                    if times != 2:
                        die('multiplication factor must be two')
                force_match(']')
            condition = None
        else:
            condition = name + ' '
            condition += accumulate_balanced(',)', swallow_end=False)
            get_token()

        return Arg(name, type_, idx, times, condition)


def print_header():
    """Prints the output file header."""
    sys.stdout.write("""\
/* Generated by generate.py. Do not modify! */
""")


def print_trailer():
    """Prints the output file trailer."""
    sys.stdout.write("""\

/*
   Local Variables:
   mode: c
   buffer-read-only: t
   End:
*/
""")


def generate_evaluate_h():
    sys.stdout.write('#include "helpers.h"\n\n')

    for op in order:
        if op.unimplemented:
            continue

        args = []
        for arg in op.args:
            if arg.idx is None:
                args += [arg.type_.c_type + arg.name]
            else:
                args += [arg.type_.c_type + arg.name + '[]']
                args += ['size_t %s' % arg.idx]
        for aux in op.aux:
            args += [aux['TYPE'].c_type + aux['NAME']]
        if not args:
            args += ['void']

        if op.block:
            statements = op.block + '\n'
        else:
            statements = '  return %s;\n' % op.expression

        sys.stdout.write('static inline %s\n' % op.returns.c_type)
        sys.stdout.write('eval_%s (%s)\n' % (op.opname, ', '.join(args)))
        sys.stdout.write('{\n')
        sys.stdout.write(statements)
        sys.stdout.write('}\n\n')


def generate_evaluate_inc():
    for op in order:
        if op.unimplemented:
            sys.stdout.write('case %s:\n' % op.opname)
            sys.stdout.write('  NOT_REACHED ();\n\n')
            continue

        decls = []
        args = []
        for arg in op.args:
            type_ = arg.type_
            if type_.c_type == 'int ':
                c_type = 'double '
                if op.absorb_miss:
                    args += ['arg_%s == SYSMIS ? INT_MIN : arg_%s'
                             % (arg.name, arg.name)]
                else:
                    args += ['arg_%s' % arg.name]
            else:
                c_type = type_.c_type
                args += ['arg_%s' % arg.name]
            if arg.idx is None:
                decl = '%sarg_%s' % (c_type, arg.name)
                if type_.role == 'any':
                    decls = ['%s = *--%s' % (decl, type_.stack)] + decls
                elif type_.role == 'leaf':
                    decls += ['%s = op++->%s' % (decl, type_.atom)]
                else:
                    assert False
            else:
                idx = arg.idx
                decls = ['%s*arg_%s = %s -= arg_%s'
                         % (c_type, arg.name, type_.stack, idx)] + decls
                decls = ['size_t arg_%s = op++->integer' % idx] + decls

                idx = 'arg_%s' % idx
                if arg.times != 1:
                    idx += ' / %s' % arg.times
                args += [idx]
        for aux in op.aux:
            type_ = aux['TYPE']
            name = aux['NAME']
            if type_.role == 'leaf':
                decls += ['%saux_%s = op++->%s'
                          % (type_.c_type, name, type_.atom)]
                args += ['aux_%s' % name]
            elif type_.name == 'expr_node':
                decls += ['%saux_%s = op++->node'
                          % (type_.c_type, name)]
                args += ['aux_%s' % name]
            elif type_.role == 'auxonly':
                args += [type_.auxonly_value]

        sysmis_cond = op.sysmis_decl('op++->integer')
        if sysmis_cond is not None:
            decls += [sysmis_cond]

        result = 'eval_%s (%s)' % (op.opname, ', '.join(args))

        stack = op.returns.stack

        sys.stdout.write('case %s:\n' % op.opname)
        if decls:
            sys.stdout.write('  {\n')
            for decl in decls:
                sys.stdout.write('    %s;\n' % decl)
            if sysmis_cond is not None:
                miss_ret = op.returns.missing_value
                sys.stdout.write('    *%s++ = force_sysmis ? %s : %s;\n'
                               % (stack, miss_ret, result))
            else:
                sys.stdout.write('    *%s++ = %s;\n' % (stack, result))
            sys.stdout.write('  }\n')
        else:
            sys.stdout.write('  *%s++ = %s;\n' % (stack, result))
        sys.stdout.write('  break;\n\n')


def generate_operations_h():
    sys.stdout.write('#include <stdlib.h>\n')
    sys.stdout.write('#include <stdbool.h>\n\n')

    sys.stdout.write('typedef enum')
    sys.stdout.write('  {\n')
    atoms = []
    for type_ in types.values():
        if type_.role != 'auxonly':
            atoms += ['OP_%s' % type_.name]

    print_operations('atom', 1, atoms)
    print_operations('function', 'OP_atom_last + 1',
                     [f.opname for f in funcs])
    print_operations('operator', 'OP_function_last + 1',
                     [o.opname for o in opers])
    print_range('OP_composite', 'OP_function_first', 'OP_operator_last')
    sys.stdout.write(',\n\n')
    print_range('OP', 'OP_atom_first', 'OP_composite_last')
    sys.stdout.write('\n  }\n')
    sys.stdout.write('operation_type, atom_type;\n')

    print_predicate('is_operation', 'OP')
    for key in ('atom', 'composite', 'function', 'operator'):
        print_predicate('is_%s' % key, 'OP_%s' % key)


def print_operations(type_, first, names):
    sys.stdout.write('    /* %s types. */\n' % type_.title())
    sys.stdout.write('    %s = %s,\n' % (names[0], first))
    for name in names[1:]:
        sys.stdout.write('    %s,\n' % name)
    print_range('OP_%s' % type_, names[0], names[-1])
    sys.stdout.write(',\n\n')


def print_range(prefix, first, last):
    sys.stdout.write('    %s_first = %s,\n' % (prefix, first))
    sys.stdout.write('    %s_last = %s,\n' % (prefix, last))
    sys.stdout.write('    n_%s = %s_last - %s_first + 1'
                   % (prefix, prefix, prefix))


def print_predicate(function, category):
    sys.stdout.write('\nstatic inline bool\n')
    sys.stdout.write('%s (operation_type op)\n' % function)
    sys.stdout.write('{\n')
    if function != 'is_operation':
        sys.stdout.write('  assert (is_operation (op));\n')
    sys.stdout.write('  return op >= %s_first && op <= %s_last;\n'
                   % (category, category))
    sys.stdout.write('}\n')


def generate_optimize_inc():
    for op in order:
        if not op.optimizable or op.unimplemented:
            sys.stdout.write('case %s:\n' % op.opname)
            sys.stdout.write('  NOT_REACHED ();\n\n')
            continue

        decls = []
        arg_idx = 0
        for arg in op.args:
            name = arg.name
            type_ = arg.type_
            c_type = type_.c_type
            if arg.idx is None:
                func = ('get_integer_arg' if type_.name == 'integer'
                        else 'get_%s_arg' % type_.atom)
                decls += ['%sarg_%s = %s (node, %s)'
                          % (c_type, name, func, arg_idx)]
            else:
                decl = 'size_t arg_%s = node->n_args' % arg.idx
                if arg_idx > 0:
                    decl += ' - %s' % arg_idx
                decls += [decl]

                decls += ['%s*arg_%s = get_%s_args  '
                          '(node, %s, arg_%s, e)'
                          % (c_type, name, type_.atom, arg_idx, arg.idx)]
            arg_idx += 1

        sysmis_cond = op.sysmis_decl('node->min_valid')
        if sysmis_cond is not None:
            decls += [sysmis_cond]

        args = []
        for arg in op.args:
            args += ['arg_%s' % arg.name]
            if arg.idx is not None:
                idx = 'arg_%s' % arg.idx
                if arg.times != 1:
                    idx += ' / %s' % arg.times
                args += [idx]

        for aux in op.aux:
            type_ = aux['TYPE']
            if type_.role == 'leaf':
                assert type_.name == 'expr_node'
                args += ['node']
            elif type_.role == 'auxonly':
                args += [type_.auxonly_value]
            else:
                assert False

        result = 'eval_%s (%s)' % (op.opname, ', '.join(args))
        if decls and sysmis_cond is not None:
            miss_ret = op.returns.missing_value
            decls += ['%sresult = force_sysmis ? %s : %s'
                      % (op.returns.c_type, miss_ret, result)]
            result = 'result'

        sys.stdout.write('case %s:\n' % op.opname)
        alloc_func = 'expr_allocate_%s' % op.returns.name
        if decls:
            sys.stdout.write('  {\n')
            for decl in decls:
                sys.stdout.write('    %s;\n' % decl)
            sys.stdout.write('    return %s (e, %s);\n' % (alloc_func, result))
            sys.stdout.write('  }\n')
        else:
            sys.stdout.write('  return %s (e, %s);\n' % (alloc_func, result))
        sys.stdout.write('\n')


def generate_parse_inc():
    members = ['""', '""', '0', '0', '0', '{}', '0', '0']
    sys.stdout.write('{%s},\n' % ', '.join(members))

    for type_ in types.values():
        if type_.role != 'auxonly':
            members = ('"%s"' % type_.name, '"%s"' % type_.human_name,
                       '0', 'OP_%s' % type_.name, '0', '{}', '0', '0')
            sys.stdout.write('{%s},\n' % ', '.join(members))

    for op in order:
        members = []
        members += ['"%s"' % op.name]

        prototype = op.prototype()
        members += ['"%s"' % prototype if prototype else 'NULL']

        members += [op.flags()]

        members += ['OP_%s' % op.returns.name]

        members += ['%s' % len(op.args)]

        arg_types = ['OP_%s' % arg.type_.name for arg in op.args]
        members += ['{%s}' % ', '.join(arg_types)]

        members += ['%s' % op.min_valid]

        members += ['%s' % (op.array_arg().times if op.array_arg() else 0)]

        sys.stdout.write('{%s},\n' % ', '.join(members))


def usage():
    print("""\
%s, for generating expression parsers and evaluators from definitions
usage: generate.py -o OUTPUT_TYPE [-i INPUT] [-h] > OUTPUT
  -i INPUT    input file containing definitions (default: operations.def)
  -o OUTPUT   output file type, one of: evaluate.h, evaluate.inc,
              operations.h, optimize.inc, parse.inc
  -h          display this help message
""" % argv0)
    sys.exit(0)


if __name__ == '__main__':
    try:
        options, args = getopt.gnu_getopt(sys.argv[1:], 'hi:o:',
                                          ['input=s',
                                           'output=s',
                                           'help'])
    except getopt.GetoptError as geo:
        die('%s: %s' % (argv0, geo.msg))

    in_file_name = 'operations.def'
    out_file_name = None
    for key, value in options:
        if key in ['-h', '--help']:
            usage()
        elif key in ['-i', '--input']:
            in_file_name = value
        elif key in ['-o', '--output']:
            out_file_name = value
        else:
            sys.exit(0)

    if out_file_name is None:
        die('%s: output file must be specified '
            '(use --help for help)' % argv0)

    in_file = open(in_file_name, 'r')

    init_all_types()
    parse_input()

    print_header()
    if out_file_name == 'evaluate.h':
        generate_evaluate_h()
    elif out_file_name == 'evaluate.inc':
        generate_evaluate_inc()
    elif out_file_name == 'operations.h':
        generate_operations_h()
    elif out_file_name == 'optimize.inc':
        generate_optimize_inc()
    elif out_file_name == 'parse.inc':
        generate_parse_inc()
    else:
        die('%s: unknown output type' % argv0)
    print_trailer()
