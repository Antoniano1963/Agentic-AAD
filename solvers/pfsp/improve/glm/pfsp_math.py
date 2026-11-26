"""Fixed Python PFSP arithmetic and elementary moves; no candidate search policy."""
import time


class PFSP:
    def __init__(self, processing, deadline, rng):
        self.p=[list(map(int,row)) for row in processing]
        self.n=len(self.p)
        self.m=len(self.p[0])
        self.deadline=deadline
        self.rng=rng
        self.total=[sum(row) for row in self.p]
        self.mean=sum(self.total)/(self.n*self.m)

    def expired(self):
        return time.monotonic()>=self.deadline

    def cost(self,seq):
        end=[0]*self.m
        for job in seq:
            before=0
            for j,v in enumerate(self.p[job]):
                before=max(before,end[j])+v
                end[j]=before
        return end[-1]

    def insert_costs(self,seq,job):
        k=len(seq); m=self.m; p=self.p
        head=[[0]*m for _ in range(k+1)]
        for i,x in enumerate(seq):
            prev=head[i]; cur=head[i+1]; row=p[x]; t=0
            for j in range(m):
                t=max(t,prev[j])+row[j]; cur[j]=t
        tail=[[0]*m for _ in range(k+1)]
        for i in range(k-1,-1,-1):
            after=tail[i+1]; cur=tail[i]; row=p[seq[i]]; t=0
            for j in range(m-1,-1,-1):
                t=max(t,after[j])+row[j]; cur[j]=t
        values=[]; row=p[job]
        for i in range(k+1):
            t=0; value=0; h=head[i]; q=tail[i]
            for j in range(m):
                t=max(t,h[j])+row[j]
                value=max(value,t+q[j])
            values.append(value)
        return values

    def insert(self,seq,job,random_ties=False):
        costs=self.insert_costs(seq,job)
        best=min(costs)
        if random_ties:
            pos=self.rng.choice([i for i,v in enumerate(costs) if v==best])
        else:
            pos=costs.index(best)
        return seq[:pos]+[job]+seq[pos:],best

    def neh(self,order=None,random_ties=False):
        order=sorted(range(self.n),key=lambda j:-self.total[j]) if order is None else list(order)
        seq=[]
        for at,job in enumerate(order):
            if self.expired():
                seq+=order[at:]; break
            seq,_=self.insert(seq,job,random_ties)
        return seq,self.cost(seq)

    def rebuild(self,partial,removed,random_ties=False):
        seq=partial[:]
        for at,job in enumerate(removed):
            if self.expired():
                seq+=removed[at:]; break
            seq,_=self.insert(seq,job,random_ties)
        return seq,self.cost(seq)

    def remove(self,seq,d):
        selected=set(self.rng.sample(seq,min(d,len(seq)-1)))
        return [j for j in seq if j not in selected],[j for j in seq if j in selected]

    def descent(self,seq,passes=1,random_order=True,random_ties=False):
        seq=seq[:]; value=self.cost(seq)
        for _ in range(passes):
            improved=False; jobs=seq[:]
            if random_order: self.rng.shuffle(jobs)
            for job in jobs:
                if self.expired(): return seq,value
                pos=seq.index(job); partial=seq[:pos]+seq[pos+1:]
                costs=self.insert_costs(partial,job); best=min(costs)
                if best<value or (random_ties and best==value):
                    locations=[i for i,v in enumerate(costs) if v==best]
                    at=self.rng.choice(locations) if random_ties else locations[0]
                    seq=partial[:at]+[job]+partial[at:]
                    improved=improved or best<value; value=best
            if not improved: break
        return seq,value

    def contributions(self,seq):
        # Marginal removal benefit, only for strategies explicitly choosing this cost.
        value=self.cost(seq)
        scores=[]
        for i,job in enumerate(seq):
            if self.expired(): break
            scores.append((value-self.cost(seq[:i]+seq[i+1:]),job))
        return scores
