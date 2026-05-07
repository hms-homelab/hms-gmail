import { Component, inject, signal, OnInit, OnDestroy } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatProgressSpinnerModule } from '@angular/material/progress-spinner';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatInputModule } from '@angular/material/input';
import { MatSlideToggleModule } from '@angular/material/slide-toggle';
import { MatTooltipModule } from '@angular/material/tooltip';
import { interval, Subscription } from 'rxjs';
import { switchMap } from 'rxjs/operators';
import { GmailService } from '../../services/gmail.service';
import { BackupStatus } from '../../models/email.model';

@Component({
  selector: 'app-backup',
  standalone: true,
  imports: [
    CommonModule, FormsModule,
    MatButtonModule, MatIconModule,
    MatProgressBarModule, MatProgressSpinnerModule,
    MatFormFieldModule, MatInputModule,
    MatSlideToggleModule, MatTooltipModule,
  ],
  template: `
    <div class="page-container">
      <h2 class="page-title">Backup</h2>

      <!-- Status card -->
      <div *ngIf="status() as s" class="card status-card">
        <div class="status-row">
          <span class="state-badge" [class]="stateClass(s.state)">{{ s.state }}</span>
          <div class="actions" *ngIf="isIdle(s.state)">
            <button mat-flat-button color="primary" class="action-btn" (click)="start()">
              <mat-icon>sync</mat-icon> Sync + Embed
            </button>
            <button mat-stroked-button class="action-btn" (click)="embedOnly()">
              <mat-icon>psychology</mat-icon> Embed Only
            </button>
            <button mat-stroked-button class="action-btn" (click)="purgeOnly()">
              <mat-icon>delete_sweep</mat-icon> Purge
            </button>
          </div>
          <div class="actions" *ngIf="!isIdle(s.state)">
            <button mat-stroked-button color="warn" class="action-btn" (click)="stop()">
              <mat-icon>stop</mat-icon> Stop
            </button>
          </div>
        </div>

        <!-- Always-visible DB stats -->
        <div class="metrics-grid">
          <div class="metric">
            <div class="metric-label">In DB</div>
            <div class="metric-value">{{ s.db_total | number }}</div>
            <div class="metric-sub">total emails</div>
          </div>
          <div class="metric">
            <div class="metric-label">Embedded</div>
            <div class="metric-value accent">{{ s.db_embedded | number }}</div>
            <div class="metric-sub">{{ embedPct(s) | number:'1.0-0' }}% of DB</div>
          </div>
          <div class="metric">
            <div class="metric-label">Pending</div>
            <div class="metric-value muted">{{ (s.db_total - s.db_embedded - s.db_failed) | number }}</div>
            <div class="metric-sub">
              <span *ngIf="s.db_failed > 0" class="failed-sub">{{ s.db_failed | number }} failed</span>
              <span *ngIf="s.db_failed === 0">not yet embedded</span>
            </div>
          </div>
        </div>
        <div class="embed-bar-wrap">
          <div class="embed-bar-fill" [style.width.%]="embedPct(s)"></div>
        </div>

        <!-- Active run progress -->
        <div *ngIf="!isIdle(s.state)" class="run-section">

          <!-- Sync / batch progress -->
          <div *ngIf="isSyncing(s.state) || s.total_batches > 0" class="run-block">
            <div class="run-header">
              <span class="run-phase"><mat-icon class="phase-icon">cloud_download</mat-icon> Fetching from Gmail</span>
              <span *ngIf="s.total_batches > 0" class="run-count">
                Batch {{ s.current_batch }}/{{ s.total_batches }}
              </span>
            </div>
            <mat-progress-bar mode="determinate"
              [value]="s.total_batches > 0 ? s.current_batch / s.total_batches * 100 : (s.total > 0 ? pct(s) : 0)"
              class="run-bar">
            </mat-progress-bar>
            <div class="run-sub sync-detail">
              <span class="new-chip">{{ s.downloaded | number }} new msgs</span>
              <span *ngIf="s.skipped > 0" class="skip-chip">{{ s.skipped | number }} already in DB</span>
            </div>
          </div>

          <!-- Embedding progress -->
          <div *ngIf="isEmbedding(s.state)" class="run-block">
            <div class="run-header">
              <span class="run-phase"><mat-icon class="phase-icon">psychology</mat-icon> Embedding</span>
              <span class="run-count" *ngIf="s.embed_total > 0">
                {{ s.embed_done | number }} / {{ s.embed_total | number }}
              </span>
            </div>
            <mat-progress-bar
              [mode]="s.embed_total > 0 ? 'determinate' : 'indeterminate'"
              [value]="embedRunPct(s)"
              class="run-bar">
            </mat-progress-bar>
            <div class="run-sub embed-sub-row" *ngIf="s.embed_total > 0">
              <span>{{ embedRunPct(s) | number:'1.0-0' }}% — {{ s.embed_total - s.embed_done | number }} remaining</span>
              <span *ngIf="s.embed_errors > 0" class="err-chip">{{ s.embed_errors | number }} errors</span>
            </div>
          </div>

          <!-- Purge / indexing spinner -->
          <div *ngIf="isPurging(s.state) || isIndexing(s.state)" class="run-block spinner-row">
            <mat-spinner diameter="16"></mat-spinner>
            <span class="run-phase-sm">{{ s.state }}…</span>
          </div>

          <div *ngIf="s.purged > 0" class="run-meta">
            <mat-icon class="meta-icon-sm">delete_sweep</mat-icon> {{ s.purged | number }} purged this run
          </div>
        </div>

        <div *ngIf="s.last_run" class="meta-row">
          <mat-icon class="meta-icon">access_time</mat-icon>
          Last run: {{ s.last_run | date:'medium' }}
        </div>
        <div *ngIf="s.next_run" class="meta-row">
          <mat-icon class="meta-icon">schedule</mat-icon>
          Next scheduled: {{ s.next_run | date:'medium' }}
        </div>
        <div *ngIf="s.last_error" class="error-row">
          <mat-icon>error</mat-icon> {{ s.last_error }}
        </div>
      </div>

      <div *ngIf="!status()" class="loading-center">
        <mat-spinner diameter="40"></mat-spinner>
      </div>

      <!-- Run options card -->
      <div class="card options-card">
        <div class="options-title">
          <mat-icon>tune</mat-icon> Run Options
        </div>

        <!-- Date range -->
        <div class="options-section-label">Date range</div>
        <div class="date-row">
          <mat-form-field appearance="outline" class="date-field">
            <mat-label>After</mat-label>
            <input matInput type="date" [(ngModel)]="opts.after_date" />
          </mat-form-field>
          <mat-form-field appearance="outline" class="date-field">
            <mat-label>Before</mat-label>
            <input matInput type="date" [(ngModel)]="opts.before_date" />
          </mat-form-field>
        </div>

        <!-- Max messages -->
        <mat-form-field appearance="outline" class="full-width">
          <mat-label>Max messages (0 = unlimited)</mat-label>
          <input matInput type="number" min="0" [(ngModel)]="opts.max_messages" />
        </mat-form-field>

        <!-- Gmail query override -->
        <mat-form-field appearance="outline" class="full-width">
          <mat-label>Gmail query filter (optional override)</mat-label>
          <input matInput [(ngModel)]="opts.sync_query"
                 placeholder="e.g. from:google.com  or  newer_than:6m" />
          <mat-icon matSuffix matTooltip="Appended to date range. Uses Gmail search syntax.">help_outline</mat-icon>
        </mat-form-field>

        <!-- Toggles -->
        <div class="toggles-row">
          <mat-slide-toggle [(ngModel)]="opts.run_purge" color="primary">
            Purge old messages (&gt;{{ purgeLabel() }} days)
          </mat-slide-toggle>
          <mat-slide-toggle [(ngModel)]="opts.purge_only_embedded" color="primary"
                            [disabled]="!opts.run_purge">
            Only purge already-embedded
          </mat-slide-toggle>
          <mat-slide-toggle [(ngModel)]="opts.run_embedding" color="primary">
            Run embedding after sync
          </mat-slide-toggle>
        </div>

        <div class="query-preview" *ngIf="queryPreview()">
          <span class="preview-label">Effective query:</span>
          <code>{{ queryPreview() }}</code>
        </div>
      </div>
    </div>
  `,
  styles: [`
    .page-title { font-size: 22px; font-weight: 500; margin-bottom: 16px; color: #e0e0e0; }

    .status-card { }
    .status-row {
      display: flex; align-items: center; justify-content: space-between; margin-bottom: 16px;
      @media (max-width: 480px) { flex-direction: column; align-items: stretch; gap: 12px; }
    }
    .actions {
      display: flex; gap: 8px; flex-wrap: wrap;
      @media (max-width: 480px) { flex-direction: column; gap: 10px; }
    }
    .action-btn {
      @media (max-width: 480px) { width: 100%; justify-content: center; }
    }
    .state-badge {
      font-size: 12px; font-weight: 600; padding: 4px 10px; border-radius: 12px;
      text-transform: uppercase; letter-spacing: 1px;
      &.idle      { background: #1b5e20; color: #a5d6a7; }
      &.backup    { background: #0d47a1; color: #90caf9; }
      &.purge     { background: #4a148c; color: #ce93d8; }
      &.indexing  { background: #e65100; color: #ffcc80; }
      &.embedding { background: #37474f; color: #b0bec5; }
      &.error     { background: #b71c1c; color: #ef9a9a; }
    }
    .metrics-grid {
      display: grid; grid-template-columns: repeat(3, 1fr); gap: 16px; margin-bottom: 12px;
      @media (max-width: 480px) { grid-template-columns: repeat(3, 1fr); gap: 8px; }
    }
    .metric { }
    .metric-label { font-size: 11px; color: #888; text-transform: uppercase; letter-spacing: 0.5px; margin-bottom: 2px; }
    .metric-value { font-size: 24px; font-weight: 500; color: #e0e0e0; line-height: 1.1;
      &.accent { color: #ce93d8; }
      &.muted  { color: #888; }
    }
    .metric-sub { font-size: 11px; color: #666; margin-top: 2px; }
    .embed-bar-wrap { background: #2a2a2a; border-radius: 4px; height: 5px; margin-bottom: 20px; overflow: hidden; }
    .embed-bar-fill { background: #ce93d8; height: 100%; border-radius: 4px; transition: width 0.5s; }

    .run-section { border-top: 1px solid #2e2e2e; padding-top: 16px; display: flex; flex-direction: column; gap: 14px; }
    .run-block { }
    .run-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 6px; }
    .run-phase { display: flex; align-items: center; gap: 6px; font-size: 13px; font-weight: 500; color: #ccc; }
    .phase-icon { font-size: 15px; height: 15px; width: 15px; color: #90caf9; }
    .run-count { font-size: 13px; color: #aaa; }
    .run-bar { margin-bottom: 4px; }
    .run-sub { font-size: 12px; color: #666; }
    .batch-chip { background: #2a2a2a; border-radius: 10px; padding: 2px 8px; font-size: 11px; color: #ce93d8; margin-left: 10px; }
    .sync-detail { display: flex; gap: 10px; flex-wrap: wrap; }
    .new-chip  { background: #1b3a1b; color: #a5d6a7; border-radius: 10px; padding: 2px 8px; font-size: 11px; }
    .skip-chip  { background: #2a2a2a; color: #888;     border-radius: 10px; padding: 2px 8px; font-size: 11px; }
    .err-chip   { background: #3e1a1a; color: #ef9a9a; border-radius: 10px; padding: 2px 8px; font-size: 11px; }
    .failed-sub { color: #ef9a9a; }
    .embed-sub-row { display: flex; gap: 10px; align-items: center; }
    .spinner-row { display: flex; align-items: center; gap: 10px; }
    .run-phase-sm { font-size: 13px; color: #888; }
    .run-meta { font-size: 12px; color: #666; display: flex; align-items: center; gap: 6px; }
    .meta-icon-sm { font-size: 14px; height: 14px; width: 14px; }
    .meta-row { display: flex; align-items: center; gap: 6px; font-size: 13px; color: #aaa; margin-bottom: 6px; }
    .meta-icon { font-size: 16px; height: 16px; width: 16px; }
    .error-row { display: flex; align-items: center; gap: 6px; color: #f44336; font-size: 13px; margin-top: 8px; }

    .options-card { }
    .options-title { display: flex; align-items: center; gap: 8px; font-size: 15px; font-weight: 500; color: #e0e0e0; margin-bottom: 20px; }
    .options-section-label { font-size: 12px; color: #aaa; text-transform: uppercase; letter-spacing: 0.5px; margin-bottom: 8px; }
    .date-row { display: flex; gap: 16px; }
    .date-field { flex: 1; }
    .full-width { width: 100%; }
    .toggles-row { display: flex; gap: 32px; flex-wrap: wrap; margin: 8px 0 16px; }
    .query-preview {
      background: #161616; border: 1px solid #333; border-radius: 6px;
      padding: 10px 14px; font-size: 13px; color: #aaa; margin-top: 4px;
      code { color: #90caf9; font-family: monospace; }
      .preview-label { color: #666; margin-right: 8px; }
    }
    .loading-center { display: flex; justify-content: center; padding: 40px; }
  `]
})
export class BackupPage implements OnInit, OnDestroy {
  private gmail = inject(GmailService);
  status = signal<BackupStatus | null>(null);
  private sub?: Subscription;

  opts = {
    after_date: '',
    before_date: '',
    max_messages: 0,
    sync_query: '',
    run_purge: true,
    purge_only_embedded: false,
    run_embedding: true,
  };

  ngOnInit() {
    this.gmail.getBackupStatus().subscribe(s => this.status.set(s));
    this.sub = interval(3000).pipe(
      switchMap(() => this.gmail.getBackupStatus())
    ).subscribe(s => this.status.set(s));
  }

  ngOnDestroy() { this.sub?.unsubscribe(); }

  pct(s: BackupStatus): number {
    return s.total > 0 ? Math.round((s.downloaded / s.total) * 100) : 0;
  }

  isSyncing(state: string): boolean {
    const s = state.toLowerCase();
    return s === 'backup_running' || s.includes('backup');
  }

  isPurging(state: string): boolean {
    return state.toLowerCase().includes('purge');
  }

  isIndexing(state: string): boolean {
    return state.toLowerCase() === 'indexing';
  }

  isEmbedding(state: string): boolean {
    return state.toLowerCase().includes('embed');
  }

  embedPct(s: BackupStatus): number {
    return s.db_total > 0 ? (s.db_embedded / s.db_total) * 100 : 0;
  }

  embedRunPct(s: BackupStatus): number {
    return s.embed_total > 0 ? (s.embed_done / s.embed_total) * 100 : 0;
  }

  isIdle(state: string): boolean {
    const s = state.toLowerCase();
    return s === 'idle' || s === 'error' || s === '';
  }

  stateClass(state: string): string {
    const s = state.toLowerCase();
    if (s === 'idle') return 'idle';
    if (s.includes('backup') || s.includes('running')) return 'backup';
    if (s.includes('purge')) return 'purge';
    if (s.includes('index')) return 'indexing';
    if (s.includes('embed')) return 'embedding';
    if (s === 'error') return 'error';
    return 'idle';
  }

  purgeLabel(): number {
    return 30; // reflects config default; could be fetched from /api/config in future
  }

  queryPreview(): string {
    const parts: string[] = [];
    if (this.opts.after_date)  parts.push('after:'  + this.opts.after_date.replace(/-/g, '/'));
    if (this.opts.before_date) parts.push('before:' + this.opts.before_date.replace(/-/g, '/'));
    if (this.opts.sync_query)  parts.push(this.opts.sync_query);
    return parts.join(' ');
  }

  start() {
    const body: Record<string, unknown> = {
      run_purge: this.opts.run_purge,
      purge_only_embedded: this.opts.purge_only_embedded,
      run_embedding: this.opts.run_embedding,
    };
    if (this.opts.after_date)   body['after_date']   = this.opts.after_date;
    if (this.opts.before_date)  body['before_date']  = this.opts.before_date;
    if (this.opts.max_messages) body['max_messages'] = this.opts.max_messages;
    if (this.opts.sync_query)   body['sync_query']   = this.opts.sync_query;
    this.gmail.startBackup(body).subscribe(
      () => this.gmail.getBackupStatus().subscribe(s => this.status.set(s))
    );
  }

  embedOnly() {
    this.gmail.startBackup({ skip_sync: true, run_purge: false, run_embedding: true }).subscribe(
      () => this.gmail.getBackupStatus().subscribe(s => this.status.set(s))
    );
  }

  purgeOnly() {
    this.gmail.startBackup({ skip_sync: true, run_purge: true, purge_only_embedded: this.opts.purge_only_embedded, run_embedding: false }).subscribe(
      () => this.gmail.getBackupStatus().subscribe(s => this.status.set(s))
    );
  }

  stop() { this.gmail.stopBackup().subscribe(); }
}
