import { Component, inject, signal, OnInit, OnDestroy } from '@angular/core';
import { CommonModule } from '@angular/common';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatProgressSpinnerModule } from '@angular/material/progress-spinner';
import { interval, Subscription } from 'rxjs';
import { switchMap } from 'rxjs/operators';
import { GmailService } from '../../services/gmail.service';
import { BackupStatus } from '../../models/email.model';

@Component({
  selector: 'app-backup',
  standalone: true,
  imports: [
    CommonModule, MatButtonModule, MatIconModule,
    MatProgressBarModule, MatProgressSpinnerModule
  ],
  template: `
    <div class="page-container">
      <h2 class="page-title">Backup</h2>

      <div *ngIf="status() as s" class="card">
        <div class="status-row">
          <span class="state-badge" [class]="stateClass(s.state)">{{ s.state }}</span>
          <div class="actions">
            <button mat-flat-button color="primary"
                    *ngIf="s.state === 'IDLE' || s.state === 'ERROR'"
                    (click)="start()">
              <mat-icon>play_arrow</mat-icon> Start
            </button>
            <button mat-stroked-button color="warn"
                    *ngIf="s.state !== 'IDLE' && s.state !== 'ERROR'"
                    (click)="stop()">
              <mat-icon>stop</mat-icon> Stop
            </button>
          </div>
        </div>

        <mat-progress-bar *ngIf="s.total > 0"
                          mode="determinate"
                          [value]="pct(s)"
                          class="progress-bar">
        </mat-progress-bar>

        <div class="stats-grid">
          <div class="stat">
            <div class="stat-label">Downloaded</div>
            <div class="stat-value">{{ s.downloaded }}</div>
          </div>
          <div class="stat">
            <div class="stat-label">Total</div>
            <div class="stat-value">{{ s.total }}</div>
          </div>
          <div class="stat">
            <div class="stat-label">Purged</div>
            <div class="stat-value">{{ s.purged }}</div>
          </div>
        </div>

        <div *ngIf="s.last_run" class="meta-row">
          <mat-icon class="meta-icon">access_time</mat-icon>
          Last run: {{ s.last_run | date:'medium' }}
        </div>
        <div *ngIf="s.next_run" class="meta-row">
          <mat-icon class="meta-icon">schedule</mat-icon>
          Next run: {{ s.next_run | date:'medium' }}
        </div>
        <div *ngIf="s.last_error" class="error-row">
          <mat-icon>error</mat-icon> {{ s.last_error }}
        </div>
      </div>

      <div *ngIf="!status()" class="loading-center">
        <mat-spinner diameter="40"></mat-spinner>
      </div>
    </div>
  `,
  styles: [`
    .page-title { font-size: 22px; font-weight: 500; margin-bottom: 16px; color: #e0e0e0; }
    .status-row { display: flex; align-items: center; justify-content: space-between; margin-bottom: 16px; }
    .state-badge {
      font-size: 12px; font-weight: 600; padding: 4px 10px; border-radius: 12px;
      text-transform: uppercase; letter-spacing: 1px;
      &.idle    { background: #1b5e20; color: #a5d6a7; }
      &.backup  { background: #0d47a1; color: #90caf9; }
      &.purge   { background: #4a148c; color: #ce93d8; }
      &.indexing{ background: #e65100; color: #ffcc80; }
      &.embedding{ background: #37474f; color: #b0bec5; }
      &.error   { background: #b71c1c; color: #ef9a9a; }
    }
    .progress-bar { margin-bottom: 16px; }
    .stats-grid { display: flex; gap: 24px; margin-bottom: 16px; }
    .stat { }
    .stat-label { font-size: 12px; color: #888; text-transform: uppercase; letter-spacing: 0.5px; }
    .stat-value { font-size: 24px; font-weight: 500; color: #e0e0e0; }
    .meta-row { display: flex; align-items: center; gap: 6px; font-size: 13px; color: #888; margin-bottom: 6px; }
    .meta-icon { font-size: 16px; }
    .error-row { display: flex; align-items: center; gap: 6px; color: #f44336; font-size: 13px; margin-top: 8px; }
    .loading-center { display: flex; justify-content: center; padding: 40px; }
  `]
})
export class BackupPage implements OnInit, OnDestroy {
  private gmail = inject(GmailService);
  status = signal<BackupStatus | null>(null);
  private sub?: Subscription;

  ngOnInit() {
    this.sub = interval(3000).pipe(
      switchMap(() => this.gmail.getBackupStatus())
    ).subscribe(s => this.status.set(s));
    this.gmail.getBackupStatus().subscribe(s => this.status.set(s));
  }

  ngOnDestroy() { this.sub?.unsubscribe(); }

  pct(s: BackupStatus): number {
    return s.total > 0 ? Math.round((s.downloaded / s.total) * 100) : 0;
  }

  stateClass(state: string): string {
    const m: Record<string, string> = {
      IDLE: 'idle', BACKUP_RUNNING: 'backup', PURGE_RUNNING: 'purge',
      INDEXING: 'indexing', EMBEDDING: 'embedding', ERROR: 'error'
    };
    return m[state] ?? 'idle';
  }

  start() { this.gmail.startBackup().subscribe(); }
  stop()  { this.gmail.stopBackup().subscribe(); }
}
