"""Restricted operator language. This is a scope guard, not an OS sandbox."""
import ast,builtins,math,random,statistics,itertools,functools,heapq,collections,types

MODULES={}
for name,module in [('math',math),('random',random),('statistics',statistics),('itertools',itertools),('functools',functools),('heapq',heapq),('collections',collections)]:
    MODULES[name]=types.SimpleNamespace(**{k:getattr(module,k) for k in dir(module) if not k.startswith('_') and (isinstance(getattr(module,k),(int,float)) or callable(getattr(module,k)))})
SAFE_NAMES='abs all any bool dict enumerate filter float frozenset int isinstance len list map max min next pow range reversed round set slice sorted str sum tuple zip ValueError TypeError RuntimeError StopIteration'.split()

def load_operator(source):
    tree=ast.parse(source)
    for node in ast.walk(tree):
        if isinstance(node,(ast.ClassDef,ast.Global,ast.Nonlocal,ast.AsyncFunctionDef)):
            raise ValueError('Only stateless functions and approved standard-library imports are allowed')
        if isinstance(node,ast.Name) and node.id.startswith('_'):raise ValueError('Private names are not allowed')
        if isinstance(node,ast.Attribute) and node.attr.startswith('_'):raise ValueError('Private attributes are not allowed')
        if isinstance(node,ast.Import):
            if any(n.name not in MODULES for n in node.names):raise ValueError('Only approved standard-library modules are allowed; no NumPy/JIT/filesystem/network')
        if isinstance(node,ast.ImportFrom) and (node.level or node.module not in MODULES or any(n.name.startswith('_') or n.name=='*' for n in node.names)):
            raise ValueError('Import not allowed')
    for node in tree.body:
        if not isinstance(node,(ast.FunctionDef,ast.Import,ast.ImportFrom,ast.Expr)):
            raise ValueError('No mutable module state; put constants inside functions')
        if isinstance(node,ast.Expr) and not isinstance(node.value,ast.Constant):raise ValueError('Top-level execution not allowed')
        if isinstance(node,ast.FunctionDef) and (node.decorator_list or node.args.defaults or any(node.args.kw_defaults)):
            raise ValueError('No decorators or default expressions')
    def safe_import(name,*args,**kwargs):
        if name not in MODULES:raise ImportError(name)
        return MODULES[name]
    safe={k:getattr(builtins,k) for k in SAFE_NAMES};safe['__import__']=safe_import
    env={'__builtins__':safe,**MODULES}
    exec(compile(tree,'<destroy_operator>','exec'),env)
    for name in ['destroy_v2','destroy_v1','destroy']:
        if name in env and callable(env[name]):return env[name]
    raise ValueError('Expected destroy_v2(sequence, processing_times)')

def validate_partition(sequence,result):
    if not isinstance(result,(tuple,list)) or len(result)!=2:raise ValueError('Return remaining_sequence, removed_jobs')
    remaining,removed=map(list,result)
    if any(type(x) is not int for x in remaining+removed):raise ValueError('Job IDs must be Python integers')
    if sorted(remaining+removed)!=sorted(sequence):raise ValueError('Missing, repeated, or unknown jobs')
    if not removed:raise ValueError('Remove at least one job')
    removed_set=set(removed)
    if remaining!=[x for x in sequence if x not in removed_set]:raise ValueError('Remaining jobs must preserve relative order; reconstruction is frozen')
    return remaining,removed
